if Code.loaded?(Igniter) do
  defmodule Mix.Tasks.ExVerity.EnsurePerms do
    use Igniter.Mix.Task

    @impl Igniter.Mix.Task
    def igniter(%{args: %{argv: argv}} = igniter) do
      IO.puts("Setting mode for files:")

      argv
      |> Enum.chunk_every(2)
      |> Enum.each(fn [filepath, chmod] ->
        IO.puts("#{filepath} (#{chmod})")
        {chmod, ""} = Integer.parse(chmod)
        File.chmod!(filepath, chmod)
      end)

      igniter
    end
  end
end
