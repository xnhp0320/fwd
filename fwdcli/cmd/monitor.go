package cmd

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"fwdcli/client"
	"fwdcli/formatter"

	"github.com/spf13/cobra"
)

var monitorInterval int

var monitorCmd = &cobra.Command{
	Use:   "monitor",
	Short: "Continuously monitor forwarding statistics",
	Long:  "Repeatedly queries get_stats from the DPDK control plane at a configurable interval, displaying a live dashboard until interrupted by SIGINT or SIGTERM.",
	PreRunE: func(cmd *cobra.Command, args []string) error {
		if monitorInterval <= 0 {
			return fmt.Errorf("invalid interval %d: must be a positive integer", monitorInterval)
		}
		return nil
	},
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
		defer stop()

		ticker := time.NewTicker(time.Duration(monitorInterval) * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return nil
			case <-ticker.C:
				resp, err := c.Send("get_stats")
				if err != nil {
					fmt.Fprintf(os.Stderr, "Error: %v\n", err)
					os.Exit(ExitConnError)
				}

				if !resp.IsSuccess() {
					fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Error)
					os.Exit(ExitServerError)
				}

				if jsonOutput {
					line, err := json.Marshal(resp.Result)
					if err != nil {
						fmt.Fprintf(os.Stderr, "Error: %v\n", err)
						os.Exit(ExitServerError)
					}
					fmt.Println(string(line))
				} else {
					output, err := formatter.FormatStatsMonitor(resp.Result, time.Now())
					if err != nil {
						fmt.Fprintf(os.Stderr, "Error: %v\n", err)
						os.Exit(ExitServerError)
					}
					fmt.Print(output)
				}
			}
		}
	},
}

func init() {
	monitorCmd.Flags().IntVar(&monitorInterval, "interval", 1, "polling interval in seconds")
	rootCmd.AddCommand(monitorCmd)
}
