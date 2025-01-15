defmodule ExVerity.DmVerity do
  def generate_tree!(%{private_key_path: priv_path, public_key_path: pub_path} = config, fs_path) when is_binary(priv_path) and is_binary(pub_path) do
    block_size = Map.get(config, :block_size, 4096)
    root_hash_path = Map.get(config, :root_hash_path, Briefly.create!())

    {:ok, data_size} = verity_setup(fs_path, root_hash_path, block_size)

    {File.read!(root_hash_path), data_size}
  end

  def generate_tree!(config, _) do
    IO.puts("Invalid config for dm-verity, missing a key in:\n")
    IO.inspect(config)
    IO.puts("\nValid config: private_key_path, public_key_path, block_size, root_hash_path")
    System.halt(1)
  end

  defp verity_setup(fs_path, root_hash_path, block_size) do
    %{size: data_size} = File.stat!(fs_path)
    case System.cmd("veritysetup", [
      "format", fs_path, fs_path,
      "--data-block-size=#{block_size}",
      "--hash-offset=#{data_size}",
      "--root-hash-file=#{root_hash_path}"
    ]
    ) do
      {_, 0} ->
        {:ok, data_size}
      {output, status} ->
        IO.puts("veritysetup exited with status: #{status}")
        IO.puts("output:")
        IO.puts(output)
    end
  end
end
