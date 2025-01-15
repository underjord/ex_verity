defmodule Mix.Tasks.ExVerity.Install do
  use Igniter.Mix.Task

  @impl Igniter.Mix.Task
  def igniter(igniter) do
    igniter
    |> Igniter.Project.Config.configure(
      "target.exs",
      :nerves,
      [:firmware, :post_processing_script],
      {:code,
        Sourceror.parse_string!("""
        Path.expand(".deps/ex_verity/priv/sign.sh")
        """)
      }
    )
  end
end
