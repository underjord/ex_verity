defmodule Mix.Tasks.ExVerity.Install do
  use Igniter.Mix.Task

  @impl Igniter.Mix.Task
  def igniter(igniter) do
    igniter
    |> Igniter.Project.Deps.set_dep_option(:ex_verity, :runtime, false)
    # Fix if you are running a custom nerves package via igniter
    |> Igniter.Project.Deps.set_dep_option(:nerves, :runtime, false)
    |> add_firmware_post_processing_script()
    |> add_rootfs_signing_key()
    |> add_rpi4_secure_boot()
  end

  defp add_firmware_post_processing_script(igniter) do
    run_script = Path.join(:code.priv_dir(:ex_verity), "run_elixir")
    IO.inspect(:code.priv_dir(Igniter.Project.Application.app_name(igniter)))
    target =
      igniter
      |> Igniter.Project.Application.priv_dir()
      |> Path.join("ex_verity/process_firmware")

    File.mkdir_p!(Path.dirname(target))

    igniter
    |> Igniter.create_new_file(
      target,
      File.read!(run_script),
      on_exists: :warning
    )
    |> Igniter.Project.Config.configure(
      "target.exs",
      :nerves,
      [:firmware, :post_processing_script],
      {:code,
       Sourceror.parse_string!("""
       Path.expand("priv/ex_verity/process_firmware")
       """)}
    )
  end

  defp add_rootfs_signing_key(igniter) do
    igniter
    |> Igniter.Project.Config.configure(
      "target.exs",
      :ex_verity,
      [:rootfs, :private_key_path],
      {:code,
       Sourceror.parse_string!("""
       System.fetch_env!("EX_VERITY_ROOTFS_PRIVATE_KEY_PATH")
       """)}
    )
    |> Igniter.Project.Config.configure(
      "target.exs",
      :ex_verity,
      [:rootfs, :public_key_path],
      {:code,
       Sourceror.parse_string!("""
       System.fetch_env!("EX_VERITY_ROOTFS_PUBLIC_KEY_PATH")
       """)}
    )
  end

  defp add_rpi4_secure_boot(igniter) do
    if Igniter.Util.IO.yes?(
         "Install support for Secure Boot on Raspberry Pi 4 and the Compute Module 4?"
       ) do
      initramfs_path = Path.join(:code.priv_dir(:ex_verity), "initramfs/rpi4-initramfs.gz")

      igniter
      |> enable_per_target_config()
      |> Igniter.Project.Config.configure(
        "rpi4.exs",
        :ex_verity,
        [:rpi4_secure_boot, :private_key_path],
        {:code,
         Sourceror.parse_string!("""
         System.fetch_env!("EX_VERITY_RPI4_BOOT_PRIVATE_KEY_PATH")
         """)}
      )
      |> Igniter.Project.Config.configure(
        "rpi4.exs",
        :ex_verity,
        [:rpi4_secure_boot, :public_key_path],
        {:code,
         Sourceror.parse_string!("""
         System.fetch_env!("EX_VERITY_RPI4_BOOT_PUBLIC_KEY_PATH")
         """)}
      )
      |> Igniter.Project.Config.configure(
        "rpi4.exs",
        :ex_verity,
        [:rpi4_secure_boot, :initramfs_path],
        initramfs_path
      )
      |> rpi4_custom_fwup_conf()
    else
      igniter
    end
  end

  defp rpi4_custom_fwup_conf(igniter) do
    dir = "config/rpi4"
    File.mkdir_p!(Path.join(dir, "fwup_include"))
    source_dir = Path.join(:code.priv_dir(:ex_verity), "rpi4")

    igniter
    |> Igniter.create_new_file(
      Path.join(dir, "fwup.conf"),
      File.read!(Path.join(source_dir, "fwup.conf")),
      on_exists: :warning
    )
    |> Igniter.create_new_file(
      Path.join(dir, "fwup_include/fwup-common.conf"),
      File.read!(Path.join(source_dir, "fwup_include/fwup-common.conf")),
      on_exists: :warning
    )
    |> Igniter.Project.Config.configure(
      "rpi4.exs",
      :nerves,
      [:firmware, :fwup_conf],
      "config/rpi4/fwup.conf"
    )
  end

  defp enable_per_target_config(igniter) do
    # This will break with more targets or multiple runs I think, it would add more and more of this
    igniter
    |> Igniter.update_elixir_file("config/target.exs", fn zipper ->
      zipper =
        Igniter.Code.Common.add_code(zipper, "import_config \"\#{Mix.target()}.exs\"",
          placement: :after
        )

      {:ok, zipper}
    end)
  end
end
