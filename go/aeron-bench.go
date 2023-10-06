package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/alpacahq/aeron-bench/internal/cmd"
)

func main() {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)

	ctx, cancel := context.WithCancel(context.Background())

	go func() {
		<-signals
		log.Println("received signal, exiting...")
		cancel()
		// allow enough time to handle context cancellation
		time.Sleep(10 * time.Second)
		log.Println("not all commands finished completely")
		os.Exit(1)
	}()
	if err := cmd.Command().ExecuteContext(ctx); err != nil {
		os.Exit(1)
	}
}
