/*
 * Copyright 2014-2023 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(__linux__)
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#if !defined(_MSC_VER)
#include <unistd.h>
#endif

#include <aeron_agent.h>
#include <aeronc.h>
#include <concurrent/aeron_atomic.h>
#include <util/aeron_parse_util.h>
#include <util/aeron_strutil.h>

#include "samples_configuration.h"
#include "sample_util.h"
#include "nms_messages.h"

const char usage_str[] =
    "[-h][-v][-c uri][-p prefix][-s stream-id]\n"
    "    -h               help\n"
    "    -v               show version and exit\n"
    "    -P               print progress\n"
    "    -p prefix        aeron.dir location specified as prefix\n"
    "    -c uri           use channel specified in uri\n"
    "    -s stream-id     stream-id to use\n"
    "    -m messages      number of messages to receive\n";

volatile bool running = true;

typedef struct handler_data
{
    aeron_subscription_t *subscription;
    rate_reporter_t *rate_reporter;
    uint64_t limit;
    uint64_t messages;
} handler_data_t;

void sigint_handler(int __attribute__((unused)) signal)
{
    AERON_PUT_ORDERED(running, false);
}

inline bool is_running(void)
{
    bool result;
    AERON_GET_VOLATILE(result, running);
    return result;
}

void poll_handler(void *clientd, const uint8_t __attribute__((unused)) * buffer, size_t length, aeron_header_t __attribute__((unused)) * header)
{
    // aeron_subscription_t *subscription = (aeron_subscription_t *)clientd;
    // aeron_subscription_constants_t subscription_constants;
    // aeron_header_values_t header_values;

    // if (aeron_subscription_constants(data->subscription, &subscription_constants) < 0)
    // {
    //     fprintf(stderr, "could not get subscription constants: %s\n", aeron_errmsg());
    //     return;
    // }

    // printf(
    //     "Message to stream %" PRId32 " from session %" PRId32 " (%" PRIu64 " bytes) <<%.*s>>\n",
    //     subscription_constants.stream_id,
    //     header_values.frame.session_id,
    //     (uint64_t)length,
    //     (int)length,
    //     buffer);

    handler_data_t *data = (handler_data_t *)clientd;
    if (data->rate_reporter != NULL)
        rate_reporter_on_message(data->rate_reporter, length);

    data->messages++;
    if (data->limit != 0 && data->messages >= data->limit)
        sigint_handler(0);
}

int main(int argc, char **argv)
{
    int status = EXIT_FAILURE, opt;

    const char *channel = DEFAULT_CHANNEL;
    const char *aeron_dir = NULL;
    const uint64_t idle_duration_ns = UINT64_C(1000) * UINT64_C(1000); /* 1ms */
    int32_t stream_id = DEFAULT_STREAM_ID;

    rate_reporter_t rate_reporter;
    bool show_rate_progress = false;

    handler_data_t data = {
        .limit = DEFAULT_NUMBER_OF_MESSAGES,
    };

    while ((opt = getopt(argc, argv, "hvPc:m:p:s:")) != -1)
    {
        switch (opt)
        {
        case 'c':
        {
            channel = optarg;
            break;
        }

        case 'm':
        {
            if (aeron_parse_size64(optarg, &data.limit) < 0)
            {
                fprintf(stderr, "malformed number of messages %s: %s\n", optarg, aeron_errmsg());
                exit(status);
            }
            break;
        }

        case 'P':
        {
            show_rate_progress = true;
            break;
        }

        case 'p':
        {
            aeron_dir = optarg;
            break;
        }

        case 's':
        {
            stream_id = (int32_t)strtoul(optarg, NULL, 0);
            break;
        }

        case 'v':
        {
            printf(
                "%s <%s> major %d minor %d patch %d git %s\n",
                argv[0],
                aeron_version_full(),
                aeron_version_major(),
                aeron_version_minor(),
                aeron_version_patch(),
                aeron_version_gitsha());
            exit(EXIT_SUCCESS);
        }

        case 'h':
        default:
            fprintf(stderr, "Usage: %s %s", argv[0], usage_str);
            exit(status);
        }
    }

    signal(SIGINT, sigint_handler);

    printf("Subscribing for %" PRIu64 " messages to %s on stream id %" PRId32 "\n",
           data.limit, channel, stream_id);

    aeron_context_t *context = NULL;
    aeron_t *aeron = NULL;
    aeron_async_add_subscription_t *async = NULL;
    aeron_fragment_assembler_t *fragment_assembler = NULL;
    if (aeron_context_init(&context) < 0)
    {
        fprintf(stderr, "aeron_context_init: %s\n", aeron_errmsg());
        goto cleanup;
    }

    if (NULL != aeron_dir)
    {
        if (aeron_context_set_dir(context, aeron_dir) < 0)
        {
            fprintf(stderr, "aeron_context_set_dir: %s\n", aeron_errmsg());
            goto cleanup;
        }
    }

    if (aeron_init(&aeron, context) < 0)
    {
        fprintf(stderr, "aeron_init: %s\n", aeron_errmsg());
        goto cleanup;
    }

    if (aeron_start(aeron) < 0)
    {
        fprintf(stderr, "aeron_start: %s\n", aeron_errmsg());
        goto cleanup;
    }

    if (aeron_async_add_subscription(
            &async,
            aeron,
            channel,
            stream_id,
            print_available_image,
            NULL,
            print_unavailable_image,
            NULL) < 0)
    {
        fprintf(stderr, "aeron_async_add_subscription: %s\n", aeron_errmsg());
        goto cleanup;
    }

    while (NULL == data.subscription)
    {
        if (aeron_async_add_subscription_poll(&data.subscription, async) < 0)
        {
            fprintf(stderr, "aeron_async_add_subscription_poll: %s\n", aeron_errmsg());
            goto cleanup;
        }

        sched_yield();
    }

    printf("Subscription channel status %" PRIu64 "\n", aeron_subscription_channel_status(data.subscription));

    if (aeron_fragment_assembler_create(&fragment_assembler, poll_handler, &data) < 0)
    {
        fprintf(stderr, "aeron_fragment_assembler_create: %s\n", aeron_errmsg());
        goto cleanup;
    }

    if (show_rate_progress)
    {
        if (rate_reporter_start(&rate_reporter, print_rate_report) < 0)
        {
            fprintf(stderr, "rate_reporter_start: %s\n", aeron_errmsg());
            goto cleanup;
        }
        data.rate_reporter = &rate_reporter;
    }

    uint64_t back_pressure_count = 0, message_sent_count = 0;
    int64_t start_timestamp_ns = 0;
    int64_t duration_ns;

    while (is_running())
    {
        int fragments_read = aeron_subscription_poll(
            data.subscription, aeron_fragment_assembler_handler, fragment_assembler, DEFAULT_FRAGMENT_COUNT_LIMIT);

        if (fragments_read < 0)
        {
            fprintf(stderr, "aeron_subscription_poll: %s\n", aeron_errmsg());
            goto cleanup;
        }

        if (start_timestamp_ns == 0 && fragments_read > 0)
            start_timestamp_ns = aeron_nano_clock();

        aeron_idle_strategy_busy_spinning_idle((void *)&idle_duration_ns, fragments_read);
    }
    duration_ns = aeron_nano_clock() - start_timestamp_ns;

    printf("Done receiving.\n");

    if (show_rate_progress)
    {
        rate_reporter_halt(&rate_reporter);
    }

    double avg_message_length = (double)sizeof(struct nms_opra_trade_t) + sizeof(struct nms_opra_quote_t) / 2.0;
    printf("Publisher back pressure ratio %g\n", (double)back_pressure_count / (double)message_sent_count);
    printf(
        "Total: %" PRId64 "ms, %.04g msgs/sec, %.04g bytes/sec, totals %" PRIu64 " messages %.04g MB payloads\n",
        duration_ns / (1000 * 1000),
        ((double)data.messages * (double)(1000 * 1000 * 1000) / (double)duration_ns),
        ((double)(data.messages * avg_message_length) * (double)(1000 * 1000 * 1000) / (double)duration_ns),
        data.messages,
        (double)data.messages * avg_message_length / (double)(1024 * 1024));

    status = EXIT_SUCCESS;

cleanup:
    aeron_subscription_close(data.subscription, NULL, NULL);
    aeron_close(aeron);
    aeron_context_close(context);
    aeron_fragment_assembler_delete(fragment_assembler);

    return status;
}

extern bool is_running(void);
