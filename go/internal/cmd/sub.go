package cmd

import (
	"fmt"
	"log"
	"strings"
	"time"

	"github.com/lirm/aeron-go/aeron/atomic"
	"github.com/lirm/aeron-go/aeron/idlestrategy"
	"github.com/lirm/aeron-go/aeron/logbuffer"
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
	"github.com/spf13/viper"

	"github.com/alpacahq/aeron-bench/internal/aeron"
	"github.com/alpacahq/ice-data-structs/opra"
)

type Cmd struct {
	*cobra.Command

	aeronOpts aeron.Opts
}

func Command() *cobra.Command {
	c := &Cmd{}
	c.Command = &cobra.Command{
		Use:   "aeron-bench",
		Short: "subscribe to aeron and print received messages",
		PersistentPreRunE: func(cmd *cobra.Command, _ []string) error {
			if err := bindFlags(cmd); err != nil {
				return err
			}
			return nil
		},
		RunE: c.run,
	}
	c.init()
	return c.Command
}

func (c *Cmd) init() {
	viper.AutomaticEnv()
	aeronFlags(c.Command, &c.aeronOpts)
}

func bindFlags(cmd *cobra.Command) error {
	var returnErr error
	cmd.Flags().VisitAll(func(f *pflag.Flag) {
		// environment variables cannot contain dashes
		name := strings.ReplaceAll(f.Name, "-", "_")
		if err := viper.BindPFlag(name, f); err != nil {
			returnErr = err
			return
		}
		if !f.Changed && viper.IsSet(name) {
			if err := cmd.Flags().Set(f.Name, fmt.Sprintf("%v", viper.Get(name))); err != nil {
				returnErr = err
			}
		}
	})
	return returnErr
}

func aeronFlags(cmd *cobra.Command, opts *aeron.Opts) {
	cmd.PersistentFlags().StringVar(&opts.Dir, "aeron-dir", "/dev/shm/aeron", "aeron IPC directory")
	cmd.PersistentFlags().StringVar(&opts.Channel, "aeron-channel", "aeron:ipc", "aeron channel")
	cmd.PersistentFlags().Int32Var(&opts.StreamID, "aeron-stream-id", 5000, "aeron stream id")
	cmd.PersistentFlags().DurationVar(&opts.Timeout, "aeron-timeout", 30*time.Second, "aeron timeout")
}

func (c *Cmd) run(cmd *cobra.Command, _ []string) error {
	conductor, err := aeron.NewConductor(c.aeronOpts)
	cobra.CheckErr(err)
	defer conductor.Close()

	return conductor.Subscribe(cmd.Context(), handler, idlestrategy.Busy{})
}

func handler(buffer *atomic.Buffer, offset int32, length int32, _ *logbuffer.Header) {
	var err error
	bytes := buffer.GetBytesArray(offset, length)

	switch bytes[0] {
	case opra.MsgTypeQuote:
		q := &opra.Quote{}
		err = q.UnmarshalUnsafe(bytes)
		if err != nil {
			log.Panicf("invalid quote: %s", err)
			return
		}
		fmt.Println(string(q.MarshalJson()))

	case opra.MsgTypeTrade:
		t := &opra.Trade{}
		err = t.UnmarshalUnsafe(bytes)
		if err != nil {
			log.Panicf("invalid trade: %s", err)
			return
		}
		fmt.Println(string(t.MarshalJson()))

	default:
		log.Printf("invalid message type: %c\n", bytes[0])
	}
}
