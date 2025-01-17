defmodule ExVerity.Rpi4SecureBootTest do
  use ExVerity.TestCase, async: false
  doctest ExVerity
  alias ExVerity.Rpi4SecureBoot

  setup do
    nerves_system_file = Briefly.create!()
    nerves_system_dir = Path.dirname(nerves_system_file)
    File.mkdir_p!(Path.join(nerves_system_dir, "images"))
    write_expected_files(nerves_system_dir)
    pub_path = Briefly.create!() <> ".pem"
    priv_path = Briefly.create!() <> ".pem"

    shell!("""
    openssl genpkey -algorithm RSA -out #{priv_path} -pkeyopt rsa_keygen_bits:2048 && \
    openssl rsa -in #{priv_path} -outform PEM -pubout -out #{pub_path}
    """)

    {:ok, %{priv: priv_path, pub: pub_path, system: nerves_system_dir}}
  end

  test "generate image", %{priv: priv, pub: pub, system: system} do
    assert {:ok, boot_img_path} =
             Rpi4SecureBoot.generate_image(%{
               private_key_path: priv,
               rootfs_public_key_path: pub,
               system_path: system
             })

    IO.inspect(boot_img_path)
  end

  test "sign image" do
    refute true
  end

  defp write_expected_files(system_dir) do
    base_path = Path.join(system_dir, "images")

    [
      "bcm2711-rpi-cm4.dtb",
      "bcm2711-rpi-4-b.dtb",
      "bcm2711-rpi-400.dtb",
      "config.txt",
      "cmdline.txt",
      "zImage",
      "rootfs.cpio.zst",
      "ramoops.dtb",
      "rootfs_public.pem",
      "rpi-firmware/fixup4x.dat",
      "rpi-firmware/start4x.elf",
      "rpi-firmware/overlays/overlay_map.dtb",
      "rpi-firmware/overlays/rpi-ft5406.dtbo",
      "rpi-firmware/overlays/rpi-backlight.dtbo",
      "rpi-firmware/overlays/w1-gpio-pullup.dtbo",
      "rpi-firmware/overlays/miniuart-bt.dtbo",
      "rpi-firmware/overlays/vc4-kms-v3d.dtbo",
      "rpi-firmware/overlays/vc4-kms-v3d-pi4.dtbo",
      "rpi-firmware/overlays/vc4-kms-dsi-7inch.dtbo",
      "rpi-firmware/overlays/tc358743.dtbo",
      "rpi-firmware/overlays/dwc2.dtbo",
      "rpi-firmware/overlays/imx219.dtbo",
      "rpi-firmware/overlays/imx296.dtbo",
      "rpi-firmware/overlays/imx477.dtbo",
      "rpi-firmware/overlays/imx708.dtbo",
      "rpi-firmware/overlays/ov5647.dtbo"
    ]
    |> Enum.each(fn filepath ->
      filepath = Path.join(base_path, filepath)
      dir = Path.dirname(filepath)
      IO.inspect(dir)

      if dir != "." do
        File.mkdir_p(dir)
        |> dbg()
      end

      File.write!(filepath, "sample")
    end)
  end

  # TODO: verify image signature (not strictly in-scope)
end
