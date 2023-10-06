package aeron

import (
	"context"
	"log"
	"sync"
	"time"

	"github.com/lirm/aeron-go/aeron"
	"github.com/lirm/aeron-go/aeron/idlestrategy"
	"github.com/lirm/aeron-go/aeron/logbuffer/term"
)

type Opts struct {
	Dir      string
	Channel  string
	StreamID int32
	Timeout  time.Duration
}

type Conductor struct {
	a        *aeron.Aeron
	channel  string
	streamID int32

	once sync.Once
}

func NewConductor(opts Opts) (*Conductor, error) {
	aeronCtx := aeron.NewContext().
		AeronDir(opts.Dir).
		MediaDriverTimeout(opts.Timeout).
		ErrorHandler(func(err error) {
			log.Panicf("aeron error: %s", err)
		})

	a, err := aeron.Connect(aeronCtx)

	return &Conductor{
		a:        a,
		channel:  opts.Channel,
		streamID: opts.StreamID,
	}, err
}

func (c *Conductor) Subscribe(ctx context.Context, handler term.FragmentHandler, idler idlestrategy.Idler) error {
	return c.SubscribeWithChannelAndID(ctx, handler, idler, c.channel, c.streamID)
}

func (c *Conductor) SubscribeWithChannelAndID(ctx context.Context, handler term.FragmentHandler, idler idlestrategy.Idler, channel string, streamID int32) error { //nolint: lll
	sub, err := c.a.AddSubscription(channel, streamID)
	if err != nil {
		return err
	}
	defer sub.Close()

	log.Printf("aeron subscription connected: channel: %s, streamID: %d\n", sub.Channel(), sub.StreamID())

	counter := 0
	assembler := aeron.NewFragmentAssembler(handler, 1024)

	for {
		fm := sub.Poll(assembler.OnFragment, 10)
		idler.Idle(fm)

		if counter%10000 == 0 {
			if ctx.Err() != nil {
				return ctx.Err()
			}
		}

		counter++
	}
}

func (c *Conductor) Close() error {
	return c.a.Close()
}
