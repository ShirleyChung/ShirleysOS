# UEFI boot

The shared UEFI loader will load ELF64 kernels and call the architecture entry
after `ExitBootServices()`. x86_64 uses OVMF; ARM64 uses EDK2 on QEMU `virt`.
