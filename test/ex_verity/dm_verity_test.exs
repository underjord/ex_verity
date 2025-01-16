defmodule ExVerity.DmVerityTest do
  use ExUnit.Case, async: false
  doctest ExVerity
  alias ExVerity.DmVerity

  setup do
    # These tests must run as root, painful as that is
    # device mappers and mounting/unmounting
    assert {"root\n", 0} = System.shell("whoami")
    verity_close()
    filepath = Briefly.create!()
    m = mnt(filepath)
    File.mkdir_p!(m)
    shell!("""
    truncate -s 10M #{filepath} && \
    mkfs -t ext4 #{filepath}
    """)

    pub_path = Briefly.create!() <> ".pem"
    priv_path = Briefly.create!() <> ".pem"
    shell!("""
    openssl genpkey -algorithm RSA -out #{priv_path} -pkeyopt rsa_keygen_bits:2048 && \
    openssl rsa -in #{priv_path} -outform PEM -pubout -out #{pub_path}
    """)

    on_exit(fn ->
      try do
        umount_mapped_if_needed(filepath)
      rescue
        _ ->
          :ok
      end
    end)

    {:ok, %{img: filepath, priv: priv_path, pub: pub_path}}
  end

  test "dm-verity prevents reading data that doesn't match signature, appended tree", %{img: img, pub: pub, priv: priv} do
    # Using 16kb chunks of data as we use a 4kb block size, should separate the data
    # This is our mock data
    one_data = kb_data(16, 1)
    two_data = kb_data(16, 2)
    three_data = kb_data(16, 3)

    # Mount the data image without any special protections
    # write two files to it
    img
    |> mount()
    |> write_data("one.txt", one_data)
    |> write_data("two.txt", two_data)
    |> assert_files(["one.txt", "two.txt"])
    |> umount()

    # Generate the hash tree and append it to the data image
    {root_hash, offset} = generate_tree(img, priv, pub)

    # Mount it with the dm-verity device mapper and verify the data is there
    img
    |> mount_mapped(root_hash, offset)
    |> assert_read_data("one.txt", one_data)
    |> assert_read_data("two.txt", two_data)
    |> umount_mapped()
    # Re-mount without device mapper, modify some data
    |> mount()
    |> write_data("one.txt", three_data)
    |> umount()
    # Mount again with device mapper
    # this may fail at mounting or at reading
    |> mount_mapped_may_error(root_hash, offset)
    |> assert_read_error("one.txt")
  end

  test "dm-verity prevents reading data that doesn't match signature, separate tree", %{img: img, pub: pub, priv: priv} do
    # Using 16kb chunks of data as we use a 4kb block size, should separate the data
    # This is our mock data
    one_data = kb_data(16, 1)
    two_data = kb_data(16, 2)
    three_data = kb_data(16, 3)
    hash_path = Briefly.create!()

    # Mount the data image without any special protections
    # write two files to it
    img
    |> mount()
    |> write_data("one.txt", one_data)
    |> write_data("two.txt", two_data)
    |> assert_files(["one.txt", "two.txt"])
    |> umount()

    # Generate the hash tree and append it to the data image
    {root_hash, _offset} = generate_tree(img, priv, pub, hash_path)

    # Mount it with the dm-verity device mapper and verify the data is there
    img
    |> mount_mapped(root_hash, hash_path)
    |> assert_read_data("one.txt", one_data)
    |> assert_read_data("two.txt", two_data)
    |> umount_mapped()
    # Re-mount without device mapper, modify some data
    |> mount()
    |> write_data("one.txt", three_data)
    |> umount()
    # Mount again with device mapper
    # this may fail at mounting or at reading
    |> mount_mapped_may_error(root_hash, hash_path)
    |> assert_read_error("one.txt")
  end

  defp kb_data(size, num) do
    bits = size * 8 * 1024
    <<num::size(bits)>>
  end

  defp generate_tree(filepath, priv, pub) do
    assert {root_hash, offset} = DmVerity.generate_tree!(%{private_key_path: priv, public_key_path: pub}, filepath)
    {root_hash, offset}
  end

  defp generate_tree(filepath, priv, pub, hash_path) do
    assert {root_hash, offset} = DmVerity.generate_tree!(%{private_key_path: priv, public_key_path: pub, hash_path: hash_path}, filepath)
    {root_hash, offset}
  end

  defp mnt(filepath) do
    dir = Path.dirname(filepath)
    Path.join(dir, "mnt")
  end

  @name "verity-test"
  defp mount_mapped(filepath, root_hash, hash_path) when is_binary(hash_path) do
    m = mnt(filepath)
    shell!("""
    veritysetup open \
      #{filepath} #{@name} #{hash_path} \
      #{root_hash} && \
      mount /dev/mapper/#{@name} #{m}
    """)

    filepath
  end

  defp mount_mapped(filepath, root_hash, offset) do
    m = mnt(filepath)
    shell!("""
    veritysetup open \
      #{filepath} #{@name} #{filepath} \
      #{root_hash} \
      --hash-offset=#{offset} && \
      mount /dev/mapper/#{@name} #{m}
    """)

    filepath
  end

  defp mount_mapped_may_error(filepath, root_hash, hash_path) when is_binary(hash_path) do
    m = mnt(filepath)
    assert {output, status} = shell("""
    veritysetup open \
      #{filepath} #{@name} #{hash_path} \
      #{root_hash} && \
      mount /dev/mapper/#{@name} #{m}
    """)

    case status do
      0 ->
        :ok
      32 ->
        assert output =~ "Verity device detected corruption"
    end

    filepath
  end

  defp mount_mapped_may_error(filepath, root_hash, offset) do
    m = mnt(filepath)
    assert {output, status} = shell("""
    veritysetup open \
      #{filepath} #{@name} #{filepath} \
      #{root_hash} \
      --hash-offset=#{offset} && \
      mount /dev/mapper/#{@name} #{m}
    """)

    case status do
      0 ->
        :ok
      32 ->
        assert output =~ "Verity device detected corruption"
    end

    filepath
  end

  defp umount_mapped(filepath) do
    m = mnt(filepath)
    shell!("""
    umount #{m} && \
    veritysetup close #{@name}
    """)

    filepath
  end

  defp umount_mapped_if_needed(filepath) do
    m = mnt(filepath)
    shell("""
    umount #{m} && \
    veritysetup close #{@name}
    """)

    filepath
  end

  defp verity_close() do
    shell("""
    veritysetup close #{@name}
    """)
  end

  defp mount(filepath) do
    m = mnt(filepath)
    File.mkdir_p!(m)
    shell!("""
    mount -o loop #{filepath} #{m}/
    """)
    filepath
  end

  defp write_data(filepath, filename, data) do
    m = mnt(filepath)
    target = Path.join(m, filename)
    target
    |> Path.dirname()
    |> File.mkdir_p!()

    File.write!(target ,data)
    filepath
  end

  defp assert_read_data(filepath, filename, data) do
    assert {:ok, ^data} =
      filepath
      |> mnt()
      |> Path.join(filename)
      |> File.read()

    filepath
  end

  defp assert_read_error(filepath, filename) do
    m = mnt(filepath)
    assert {:error, :enoent} =
      m
      |> Path.join(filename)
      |> File.read()

    filepath
  end

  defp assert_files(filepath, files) do
    m = mnt(filepath)
    list = File.ls!(m)
    assert Enum.all?(files, fn file ->
      file in list
    end)
    filepath
  end

  defp umount(filepath) do
    m = mnt(filepath)
    shell!("""
    umount #{m}/
    """)
    filepath
  end

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
