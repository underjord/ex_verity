defmodule ExVerity do
  @moduledoc """
  This dependency mostly wraps up the shell script in `priv/sign.sh`
  provides install conveniences and basic documentation for running
  a signed Nerves root filesystem using the `dm-verity` tooling.
  """

  require Logger

  def run(fs_path) do
    config = Application.get_all_env(:ex_verity)

    if get_in(config, [:rootfs, :private_key_path]) &&
       get_in(config, [:rootfs, :public_key_path]) do
      {root_hash, hash_offset} =
        config[:rootfs]
        |> Map.new()
        |> ExVerity.DmVerity.generate_tree!(fs_path)
      Logger.info("Root hash: #{root_hash}")
      Logger.info("Hash offset: #{hash_offset}")
    else
      Logger.info("dm-verity not configured")
    end
    if get_in(config, [:rpi4_secure_boot, :private_key_path]) do
      {:ok, _image_path} =
        config
        |> Map.new()
        |> ExVerity.Rpi4SecureBoot.generate_image()
#      ExVerity.Pi4SecureBoot.sign_image(image_path)
    end
  end

end
