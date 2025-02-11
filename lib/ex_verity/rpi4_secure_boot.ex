defmodule ExVerity.Rpi4SecureBoot do
  require Logger
  # NERVES_SYSTEM/images is where the good stuff lives, mmkay
  def generate_image(%{private_key_path: priv_key} = config) do
    Logger.info("Setting up Raspberry Pi 4 Secure Boot")
    Logger.info("Using private key: #{priv_key}")

    rpi4_dir = Path.join(:code.priv_dir(:ex_verity), "rpi4")

    rootfs_pubkey =
      config[:rootfs_public_key_path] ||
        Application.get_env(:ex_verity, :rootfs, [])[:public_key_path]

    rootfs_path = config[:initramfs_path]

    if is_nil(rootfs_pubkey) do
      Logger.warning(
        "No root filesystem public key specified for ex_verity. This RPi 4 will not be able to verify the root filesystem. See examples for configuration in the documentation."
      )
    end

    config_file = Map.get(config, :config_file, Path.join(rpi4_dir, "boot_img/boot_fwup.conf"))
    config_txt = Path.join(rpi4_dir, "boot_img/config.txt")
    cmdline_txt = Path.join(rpi4_dir, "boot_img/cmdline.txt")

    system_path = config[:system_path] || System.fetch_env!("NERVES_SYSTEM")
    binary = Map.get(config, :binary_path, "fwup")

    fwup(
      binary,
      system_path,
      config_file,
      config_txt,
      cmdline_txt,
      rootfs_pubkey,
      rootfs_path
    )
  end

  defp fwup(
         binary,
         system_path,
         config_file,
         config_txt,
         cmdline_txt,
         rootfs_pubkey,
         rootfs_path
       ) do
    images_dir = Path.join(system_path, "images")
    tmp_dir = Path.dirname(Briefly.create!())
    fw_file = Path.join(tmp_dir, "boot.fw")
    boot_file = Path.join(images_dir, "boot.img")

    with {:create, {_, 0}} <-
           {:create,
            fwup_create(
              binary,
              config_file,
              fw_file,
              images_dir,
              config_txt,
              cmdline_txt,
              rootfs_pubkey,
              rootfs_path
            )},
         {:apply, {_, 0}} <- {:apply, fwup_apply(binary, boot_file, fw_file)} do
      {:ok, boot_file}
    else
      {tag, {output, status}} ->
        IO.puts("#{binary} for #{tag} exited with status: #{status}")
        IO.puts("output:")
        IO.puts(output)
        System.halt(1)
    end
  end

  defp fwup_create(
         binary,
         config_file,
         fw_file,
         images_dir,
         config_txt,
         cmdline_txt,
         rootfs_pub_key,
         rootfs_path
       ) do
    System.cmd(
      binary,
      [
        "-c",
        "-f",
        config_file,
        "-o",
        fw_file
      ],
      env: %{
        "SOURCE" => images_dir,
        "CONFIG_TXT" => config_txt,
        "CMDLINE_TXT" => cmdline_txt,
        "ROOTFS_PUB_KEY" => rootfs_pub_key,
        "ROOTFS" => rootfs_path
      }
    )
  end

  defp fwup_apply(binary, boot_file, fw_file) do
    System.cmd(
      binary,
      [
        "-a",
        "-i",
        fw_file,
        "-t",
        "complete",
        "-d",
        boot_file
      ]
    )
  end

  def sign_image(image_path, private_key_path) do
    dir = Path.dirname(image_path)
    signature_path = Path.join(dir, "boot.sig")
    sig_temp_file = Briefly.create!()

    with {_, 0} <-
           System.cmd("openssl", [
             "dgst",
             "-sha256",
             "-sign",
             private_key_path,
             "-out",
             sig_temp_file,
             image_path
           ]),
         {shasum, 0} <- System.cmd("sha256sum", [image_path]),
         {hexsignature, 0} <- System.shell("xxd -c 4096 -p < #{sig_temp_file}") do
      ts =
        DateTime.utc_now()
        |> DateTime.to_unix()

      signature = """
      #{shasum}
      ts: #{ts}
      rsa2048: #{hexsignature}
      """

      case File.write(signature_path, signature) do
        :ok ->
          {:ok, signature_path}

        error ->
          error
      end
    else
      _ ->
        {:error, :signature_failed}
    end
  end

  def copy_outer_txts!(image_path) do
    dir = Path.dirname(image_path)
    rpi4_dir = Path.join(:code.priv_dir(:ex_verity), "rpi4")
    File.cp!(Path.join(rpi4_dir, "boot_img/config.txt"), Path.join(dir, "outer_config.txt"))
    File.cp!(Path.join(rpi4_dir, "boot_img/cmdline.txt"), Path.join(dir, "outer_cmdline.txt"))
    :ok
  end
end
