defmodule ExVerity.TestCase do
  defmacro __using__(_) do
    quote do
      use ExUnit.Case

      defp shell(cmd) do
        System.shell(cmd, stderr_to_stdout: true)
      end

      defp shell!(cmd) do
        case System.shell(cmd, stderr_to_stdout: true) do
          {output, 0} ->
            {:ok, output}

          {output, status} ->
            IO.inspect(cmd, label: "Error in shell cmd")
            IO.inspect(status, label: "status")
            IO.puts("Output:")
            IO.puts(output)
            assert status == 0
        end
      end
    end
  end
end
