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
#include <string.h>

#if !defined(_MSC_VER)
#include <unistd.h>
#endif

#include <aeronc.h>
#include <aeron_agent.h>
#include <aeron_alloc.h>
#include <concurrent/aeron_atomic.h>
#include <util/aeron_strutil.h>
#include <util/aeron_parse_util.h>

#include "sample_util.h"
#include "samples_configuration.h"
#include "nms_messages.h"
#include "xtypes.h"

const char usage_str[] =
    "[-h][-P][-v][-c uri][-L length][-l linger][-m messages][-p prefix][-s stream-id]\n"
    "    -h               help\n"
    "    -v               show version and exit\n"
    "    -P               print progress\n"
    "    -x               exclusive\n"
    "    -p prefix        aeron.dir location specified as prefix\n"
    "    -c uri           use channel specified in uri\n"
    "    -s stream-id     stream-id to use\n"
    "    -l linger        linger at end of publishing for linger seconds\n"
    "    -m messages      number of messages to send (0: never stops)\n";

volatile bool running = true;

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

int main(int argc, char **argv)
{
    int status = EXIT_FAILURE, opt;

    const char *channel = DEFAULT_CHANNEL;
    const char *aeron_dir = NULL;
    uint64_t linger_ns = DEFAULT_LINGER_TIMEOUT_MS * UINT64_C(1000) * UINT64_C(1000);
    uint64_t messages = 0;
    int32_t stream_id = DEFAULT_STREAM_ID;
    bool use_exclusive = false;

    rate_reporter_t rate_reporter;
    bool show_rate_progress = false;

    while ((opt = getopt(argc, argv, "hPvxc:L:l:m:p:s:")) != -1)
    {
        switch (opt)
        {
        case 'c':
        {
            channel = optarg;
            break;
        }

        case 'l':
        {
            if (aeron_parse_duration_ns(optarg, &linger_ns) < 0)
            {
                fprintf(stderr, "malformed linger %s: %s\n", optarg, aeron_errmsg());
                exit(status);
            }
            break;
        }

        case 'm':
        {
            if (aeron_parse_size64(optarg, &messages) < 0)
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

        case 'x':
        {
            use_exclusive = true;
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

    printf("Streaming %" PRIu64 " messages to %s on stream id %" PRId32 "\n",
           messages, channel, stream_id);

    uint8_t *message = NULL;
    aeron_context_t *context = NULL;
    aeron_t *aeron = NULL;
    aeron_buffer_claim_t buffer_claim;
    aeron_async_add_exclusive_publication_t *easync = NULL;
    aeron_exclusive_publication_t *epublication = NULL;
    aeron_async_add_publication_t *async = NULL;
    aeron_publication_t *publication = NULL;
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

    if (use_exclusive)
    {
        if (aeron_async_add_exclusive_publication(&easync, aeron, channel, stream_id) < 0)
        {
            fprintf(stderr, "aeron_async_add_exclusive_publication: %s\n", aeron_errmsg());
            goto cleanup;
        }

        while (NULL == epublication)
        {
            if (aeron_async_add_exclusive_publication_poll(&epublication, easync) < 0)
            {
                fprintf(stderr, "aeron_async_add_exclusive_publication_poll: %s\n", aeron_errmsg());
                goto cleanup;
            }
            sched_yield();
        }

        printf("Publication channel status %" PRIu64 "\n", aeron_exclusive_publication_channel_status(epublication));
    }
    else
    {
        if (aeron_alloc((void **)&message, sizeof(union option_t)) < 0)
        {
            fprintf(stderr, "allocating message: %s\n", aeron_errmsg());
            goto cleanup;
        }
        memset(message, 0, sizeof(union option_t));

        if (aeron_async_add_publication(&async, aeron, channel, stream_id) < 0)
        {
            fprintf(stderr, "aeron_async_add_publication: %s\n", aeron_errmsg());
            goto cleanup;
        }

        while (NULL == publication)
        {
            if (aeron_async_add_publication_poll(&publication, async) < 0)
            {
                fprintf(stderr, "aeron_async_add_publication_poll: %s\n", aeron_errmsg());
                goto cleanup;
            }
            sched_yield();
        }

        printf("Publication channel status %" PRIu64 "\n", aeron_publication_channel_status(publication));
    }

    if (show_rate_progress)
    {
        if (rate_reporter_start(&rate_reporter, print_rate_report) < 0)
        {
            fprintf(stderr, "rate_reporter_start: %s\n", aeron_errmsg());
            goto cleanup;
        }
    }

    struct nms_opra_trade_t trade = {
        .symbol = "AAPL",
        .condition = 'a',
        .exchange = 'A',
        .strike_price = 123456,
        .premium_price = 987654,
        .volume = 111,
        .expiration = {'T', 23, 18}, // 2023-08-18 Put
    };

    struct nms_opra_quote_t quote = {
        .symbol = "AAPL",
        .condition = 'a',
        .bid_price = 123456,
        .ask_price = 987654,
        .bid_size = 111,
        .ask_size = 999,
        .bid_exchange = 'A',
        .ask_exchange = 'Z',
        .expiration = {'L', 23, 18}, // 2023-12-18 Call
    };

    uint64_t back_pressure_count = 0, message_sent_count = 0;
    int64_t start_timestamp_ns, duration_ns;

    start_timestamp_ns = aeron_nano_clock();
    int message_length = 0;
    if (use_exclusive)
    {
        for (uint64_t i = 0; (messages == 0 || i < messages) && is_running();)
        {
            // +1 is the type
            message_length = (i % 2 == 0) ? sizeof(struct nms_opra_trade_t) + 1 : sizeof(struct nms_opra_quote_t) + 1;
            int64_t result = aeron_exclusive_publication_try_claim(
                epublication,
                message_length,
                &buffer_claim);

            if (result == AERON_PUBLICATION_ERROR)
            {
                fprintf(stderr, "aeron_exclusive_publication_try_claim: %s\n", aeron_errmsg());
                break;
            }
            else if (result < 0)
            {
                back_pressure_count++;
                aeron_idle_strategy_busy_spinning_idle(NULL, 0);
            }
            else
            {
                xuint8 shift = i % 26;
                if (i % 2 == 0)
                {
                    trade.timestamp = aeron_nano_clock();
                    trade.condition = 'a' + shift;
                    trade.exchange = 'A' + shift;
                    trade.volume = 100 + shift;
                    buffer_claim.data[0] = 't';
                    *((struct nms_opra_trade_t *)&buffer_claim.data[1]) = trade;
                }
                else
                {
                    quote.timestamp = aeron_nano_clock();
                    quote.condition = 'a' + shift;
                    quote.ask_exchange = 'A' + shift;
                    quote.bid_exchange = 'Z' - shift;
                    quote.ask_size = 201 + shift;
                    quote.bid_size = 199 - shift;
                    buffer_claim.data[0] = 'q';
                    *((struct nms_opra_quote_t *)&buffer_claim.data[1]) = quote;
                }
                aeron_buffer_claim_commit(&buffer_claim);
                if (show_rate_progress)
                    rate_reporter_on_message(&rate_reporter, message_length);

                message_sent_count++;
                i++;
            }
        }
    }
    else
    {
        for (uint64_t i = 0; (messages == 0 || i < messages) && is_running(); i++)
        {
            if (i % 2 == 0)
            {
                message_length = sizeof(struct nms_opra_trade_t);
                trade.timestamp = aeron_nano_clock();
                *((struct nms_opra_trade_t *)message) = trade;
            }
            else
            {
                message_length = sizeof(struct nms_opra_quote_t);
                quote.timestamp = aeron_nano_clock();
                *((struct nms_opra_quote_t *)message) = quote;
            }
            while (aeron_publication_offer(publication, message, message_length, NULL, NULL) < 0)
            {
                ++back_pressure_count;
                if (!is_running())
                    break;
                aeron_idle_strategy_busy_spinning_idle(NULL, 0);
            }

            if (show_rate_progress)
                rate_reporter_on_message(&rate_reporter, message_length);

            message_sent_count++;
        }
    }
    duration_ns = aeron_nano_clock() - start_timestamp_ns;

    printf("Done sending.\n");

    if (show_rate_progress)
    {
        rate_reporter_halt(&rate_reporter);
    }

    double avg_message_length = (double)sizeof(struct nms_opra_trade_t) + sizeof(struct nms_opra_quote_t) / 2.0;
    printf("Publisher back pressure ratio %g\n", (double)back_pressure_count / (double)message_sent_count);
    printf(
        "Total: %" PRId64 "ms, %.04g msgs/sec, %.04g bytes/sec, totals %" PRIu64 " messages %.04g MB payloads\n",
        duration_ns / (1000 * 1000),
        ((double)messages * (double)(1000 * 1000 * 1000) / (double)duration_ns),
        ((double)(messages * avg_message_length) * (double)(1000 * 1000 * 1000) / (double)duration_ns),
        messages,
        (double)messages * avg_message_length / (double)(1024 * 1024));

    if (linger_ns > 0)
    {
        printf("Lingering for %" PRIu64 " nanoseconds\n", linger_ns);
        aeron_nano_sleep(linger_ns);
    }

    status = EXIT_SUCCESS;

cleanup:
    aeron_exclusive_publication_close(epublication, NULL, NULL);
    aeron_publication_close(publication, NULL, NULL);
    aeron_close(aeron);
    aeron_context_close(context);
    if (use_exclusive)
        aeron_free(message);

    return status;
}

extern bool is_running(void);
