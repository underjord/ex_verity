defmodule ExVerity do
  @moduledoc """
  This dependency mostly wraps up the shell script in `priv/sign.sh`
  provides install conveniences and basic documentation for running
  a signed Nerves root filesystem using the `dm-verity` tooling.
  """

  def run do
    config = Application.get_all_env(:ex_verity)

    if get_in(config, [:rootfs, :private_key_path]) &&
       get_in(config, [:rootfs, :public_key_path]) do
      root_hash =
        config[:rootfs]
        |> Map.new()
        |> ExVerity.DmVerity.generate_tree!()
      IO.inspect(root_hash)
    end

    # if get_in(config, [:pi4_secure_boot, :private_key_path]) do
    #   {:ok, image_path} = ExVerity.Pi4SecureBoot.generate_image()
    #   ExVerity.Pi4SecureBoot.sign_image(image_path)
    # end
  end
end
