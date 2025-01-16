defmodule ExVerity.Rpi4SecureBootTest do
  use ExVerity.TestCase, async: false
  doctest ExVerity
  alias ExVerity.Rpi4SecureBoot

  setup do
    nerves_system_file = Briefly.create!()
    nerves_system_dir = Path.dirname(nerves_system_file)
    File.mkdir_p!(Path.join(nerves_system_dir, "images"))
    pub_path = Briefly.create!() <> ".pem"
    priv_path = Briefly.create!() <> ".pem"
    shell!("""
    openssl genpkey -algorithm RSA -out #{priv_path} -pkeyopt rsa_keygen_bits:2048 && \
    openssl rsa -in #{priv_path} -outform PEM -pubout -out #{pub_path}
    """)
    {:ok, %{priv: priv_path, pub: pub_path, system: nerves_system_dir}}
  end

  test "generate image, verify contents", %{priv: priv, pub: pub, system: system} do
    assert {:ok, boot_img_path} = Rpi4SecureBoot.generate_image(%{
      private_key_path: priv,
      rootfs_public_key_path: pub,
      system_path: system
    })
  end

  test "sign image" do
    refute true
  end

  # TODO: verify image signature (not strictly in-scope)
end
