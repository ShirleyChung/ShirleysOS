#pragma once

#include <cstddef>
#include <cstdint>

// virtio over MMIO：QEMU 的 ARM virt 機器在固定位址放了一排 virtio-mmio 插槽，
// 沒有接裝置的插槽讀出來的裝置編號是 0。
//
// virtio over MMIO: QEMU's ARM virt machine places a row of virtio-mmio slots
// at fixed addresses, and a slot with nothing attached reads device ID 0.
namespace shirley::platform::arm {

// QEMU virt 的插槽位置：0x0a000000 起，32 個，每個 0x200 位元組。
// The slots on QEMU virt: 32 of them from 0x0a000000, 0x200 bytes apart.
constexpr std::uintptr_t virt_virtio_mmio_base = 0x0a000000;
constexpr std::size_t virt_virtio_mmio_slots = 32;
constexpr std::size_t virt_virtio_mmio_stride = 0x200;

// 檢查每個插槽，把 legacy（版本 1）的 virtio-blk 裝置交給 virtio 驅動程式。
// 需要堆積。插槽依位址由高往低檢查，因為 QEMU 把命令列上第一個裝置放在最後
// 一個插槽；這樣第一個 -drive 才會是 vda。
//
// Check every slot and hand each legacy (version 1) virtio-blk device to the
// virtio driver. Needs the heap. Slots are checked from the highest address
// down, because QEMU puts the first device on its command line in the last
// slot; that way the first -drive becomes vda.
void virtio_mmio_probe(std::uintptr_t base, std::size_t slots, std::size_t stride);

} // namespace shirley::platform::arm
