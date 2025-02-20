defmodule Mix.Tasks.ExVerity.Initramfs do
  @moduledoc ""
  @shortdoc ""

  use Mix.Task

  require Logger

  @impl Mix.Task
  def run(_args) do
    Application.ensure_loaded(:ex_verity)
    default_initramfs_project = Path.join(:code.priv_dir(:ex_verity), "initramfs")

    case System.cmd(Path.join(default_initramfs_project, "build-one.sh"), ["rpi4"],
           cd: default_initramfs_project,
           into: IO.stream()
         ) do
      {_output, 0} ->
        :ok

      {_output, status} ->
        IO.puts("Building initramfs exited with status: #{status}")
        System.halt(1)
    end

    Logger.info("Initramfs built.")
  end
end
