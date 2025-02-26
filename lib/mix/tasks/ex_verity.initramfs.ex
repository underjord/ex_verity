defmodule Mix.Tasks.ExVerity.Initramfs do
  @moduledoc ""
  @shortdoc ""

  use Mix.Task

  require Logger

  @platforms ["rpi4"]

  @impl Mix.Task
  def run([platform]) when platform in @platforms do
    if Mix.target() != :host do
      Mix.shell().info(
        "This needs to run under MIX_TARGET=host, your mix target: #{Mix.target()}"
      )

      System.halt(3)
    end

    Application.ensure_loaded(:ex_verity)
    default_initramfs_project = Path.join(:code.priv_dir(:ex_verity), "initramfs")

    case System.cmd(Path.join(default_initramfs_project, "build-one.sh"), [platform],
           cd: default_initramfs_project,
           into: IO.stream()
         ) do
      {_output, 0} ->
        :ok

      {_output, status} ->
        IO.puts("Building initramfs exited with status: #{status}")
        System.halt(1)
    end

    Mix.shell().info("Initramfs built.")
  end

  def run(args) do
    Mix.shell().error(
      "Must be run with a valid platform. Currently supported are: #{inspect(@platforms)}"
    )

    Mix.shell().info("You provided args: #{inspect(args)}")
    System.halt(2)
  end
end
