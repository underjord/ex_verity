defmodule ExVerity.Rpi4SecureBoot do
  require Logger
  # NERVES_SYSTEM/images is where the good stuff lives, mmkay
  def generate_image(%{private_key_path: priv_key} = config) do
    Logger.info("Setting up Raspberry Pi 4 Secure Boot")
    Logger.info("Using private key: #{priv_key}")

    rpi4_dir = Path.join(:code.priv_dir(:ex_verity), "rpi4")
    rootfs_pubkey = Application.get_env(:ex_verity, :rootfs, [])[:public_key_path]
    if is_nil(rootfs_pubkey) do
      Logger.warning("No root filesystem public key specified for ex_verity. This RPi 4 will not be able to verify the root filesystem. See examples for configuration in the documentation.")
    end
    config_file = Map.get(config, :config_file, Path.join(rpi4_dir, "boot_img/boot_img.cfg"))
    config_txt = Path.join(rpi4_dir, "boot_img/config.txt")
    cmdline_txt = Path.join(rpi4_dir, "boot_img/cmdline.txt")

    system_path = System.fetch_env!("NERVES_SYSTEM")
    binary = Map.get(config, :binary_path, "veritysetup")
    genimage(binary, system_path, config_file, config_txt, cmdline_txt, rootfs_pubkey)
  end

  defp genimage(binary, system_path, config_file, config_txt, cmdline_txt, rootfs_pubkey) do
    genimage_tmp = Path.join(system_path, ".tmp-boot-img")
    File.rm_rf!(genimage_tmp)
    images_dir = Path.join(system_path, "images")
    rootpath_tmp = Path.join(system_path, ".tmp-rootpath")
    reset_dir(rootpath_tmp)
    files_dir = Path.join(system_path, ".boot-img")
    reset_dir(files_dir)

    File.rm_rf(files_dir)

    File.cp_r!(images_dir, files_dir)
    File.cp!(config_txt, Path.join(files_dir, "config.txt"))
    File.cp!(cmdline_txt, Path.join(files_dir, "cmdline.txt"))
    if rootfs_pubkey do
      File.cp!(rootfs_pubkey, Path.join(files_dir, "rootfs_public.pem"))
    end

    case System.cmd(binary, [
      "--rootpath", rootpath_tmp,
      "--tmppath", genimage_tmp,
      "--inputpath", files_dir,
      "--outputpath", images_dir,
      "--config", config_file
    ]) do
      {_, 0} -> {:ok, Path.join(images_dir, "boot.img")}
      {output, status} ->
        IO.puts("genimage exited with status: #{status}")
        IO.puts("output:")
        IO.puts(output)
        System.halt(1)
    end
  end

  defp reset_dir(path) do
    File.rm_rf(path)
    File.mkdir_p!(path)
  end

  def sign_image(image_path) do
    IO.puts("img: #{image_path}")
  end

end
