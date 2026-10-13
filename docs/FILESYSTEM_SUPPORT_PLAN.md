# ext4 / NTFS / exFAT 支援計劃 / Filesystem Support Plan

這份文件規劃 ShirleyOS 如何支援 ext4、NTFS、exFAT。它建立在現有的
`vfs::FileSystem` 介面與 `io::BlockDevice` 之上，不改變 VFS 的分層。

This plan covers ext4, NTFS and exFAT support. It builds on the existing
`vfs::FileSystem` interface and `io::BlockDevice`, and does not change the VFS
layering.

## 進度 / Status (2026-10-13)

| 項目 | 狀態 |
| --- | --- |
| 0.1 核心堆積 | 完成：`kernel/memory/heap.cpp`，`.bss` 中 1 MiB。 |
| 0.2 區塊快取 | 完成：`kernel/io/block_cache.cpp`（`io::BlockCache`）。 |
| 0.3 分割表 | 完成：`kernel/fs/partition.cpp`，MBR（含邏輯分割區）與 GPT。 |
| 0.4 磁碟驅動 | 完成（QEMU）：`drivers/virtio/block.cpp`，x86 virtio-PCI、ARM virtio-mmio。 |
| 0.5 VFS 寫入擴充 | 未開始。 |
| 0.6 名稱與路徑 | 完成：名稱 255 位元組、路徑 512 位元組；`kernel/text/utf.cpp`。 |
| 0.7 掛載指令 | 完成：`/bin/init` 與核心 shell 的 `mount <dev> <path>`／`umount`（系統呼叫 12、13），目前只認 exFAT。 |
| 0.8 mount 物件化 | 完成：exFAT 是物件，`kernel/fs/device_mount.cpp` 管理實例。 |
| FS-1 exFAT 唯讀 | 完成：`kernel/fs/exfat.cpp`；測試映像由 `tools/make-exfat-image.py` 產生。 |

以下第 1 節描述的是開始實作前的狀態。
Section 1 below describes the state before implementation began.

## 1. 現況與阻礙 / Current state and blockers

已有 / Already in place:

- `vfs::FileSystem`：`root`、`lookup`、`list`、`read`、`write`、`writable`，
  每個節點帶 `id`（檔案系統私有）。新檔案系統照 `shrfs_vfs.cpp` 的方式實作即可。
- `io::BlockDevice`：固定區塊大小的讀寫，越界回傳 `OutOfRange`。
- VFS 掛載表（`max_mounts = 4`）、最長前綴比對。

缺少 / Missing (必須先補，否則三種格式都無法開始):

| 阻礙 | 說明 | 影響 |
| --- | --- | --- |
| 真實磁碟驅動 | 目前只有 `ram0`。USB 安裝程式是主機端工具，核心還讀不到磁碟。 | 沒有裝置可掛載 |
| 核心堆積 | 沒有動態配置器，`vfs` 與 `shrfs` 都是固定表。 | ext4/NTFS 需要快取與暫存結構 |
| 分割表解析 | 沒有 MBR/GPT。 | 找不到分割區 |
| `shirley::fs` 是全域函式 | 只能掛一份 SHRFS。 | 新檔案系統必須是物件 |
| VFS 缺 `mkdir/unlink/rename/truncate/stat 時間戳` | `FileSystem` 介面只有讀寫。 | 無法寫入類操作 |
| 名稱上限 55、路徑 128 | exFAT/NTFS 名稱最長 255 UTF-16 單元，ext4 255 位元組。 | 長檔名會被拒絕 |
| 無 UTF-16 轉換、無 UTC 時間 | exFAT/NTFS 都用 UTF-16LE 與 FILETIME。 | 檔名比對與時間戳 |

## 2. 建議順序 / Recommended order

**exFAT → ext4 → NTFS。**

1. **exFAT**：格式最小、規格公開完整、結構平坦（FAT + 目錄項目集合）；USB
   隨身碟最常見，也能驗證整套驅動與 VFS 擴充是否正確。
2. **ext4**：Linux 根檔案系統的目標，且是 ShirleyOS 未來最合理的可寫系統碟。
   複雜度在 extent 樹與 journal。
3. **NTFS**：最複雜（MFT、屬性、run list、壓縮、索引 B+ 樹），先只做唯讀，
   寫入最後再評估。

## 3. 階段 / Phases

### 階段 0：基礎建設 / Foundations (所有格式的前置)

| 工作 | 內容 |
| --- | --- |
| 0.1 核心堆積 | 在 `kernel/memory/` 加 `kmalloc/kfree`（先 bump + free list，頁面來自 `page_allocator`）。 |
| 0.2 區塊快取 | `kernel/io/block_cache.cpp`：以 `(device, block)` 為鍵的 LRU 快取，write-back + `sync()`。三種檔案系統共用，取代各自單一緩衝區。 |
| 0.3 分割表 | `kernel/fs/partition.cpp`：MBR 與 GPT 解析，每個分割區包成 `io::BlockDevice`（偏移 + 長度），以 `sda1` 之類名稱登記到裝置註冊表。 |
| 0.4 磁碟驅動 | M5 的 virtio-blk（QEMU 兩個架構都有）。真機 USB/NVMe/AHCI 留給 M7。**開發與測試全用 virtio-blk + 主機端建好的映像。** |
| 0.5 VFS 擴充 | `FileSystem` 加 `create`、`mkdir`、`unlink`、`rmdir`、`rename`、`truncate`、`sync`、`stat`（大小、時間、模式）。預設實作回傳 Unsupported，既有 SHRFS 與 devfs 不用改。 |
| 0.6 名稱與路徑 | 放寬 `max_name_length` 到 255 位元組（UTF-8）；`max_path_length` 到 512。檔案系統內部負責 UTF-16 ↔ UTF-8 轉換（`kernel/text/utf.cpp`）。 |
| 0.7 檔案系統登錄 | `vfs::probe(device)`：依序呼叫各驅動的 `identify()` 以魔數判斷格式，再 `mount()`。新增 shell 指令 `mount <dev> <path>`、`umount`。 |
| 0.8 mount 物件化 | 驅動做成類別，一個實例一個卷冊；`max_mounts` 視需要調高。 |

驗證：host smoke test 用 `RamDisk` 載入主機端產生的映像（見第 5 節），不需要 QEMU。

### 階段 1：exFAT

- **識別**：boot sector 偏移 3 的 `"EXFAT   "`；驗證 boot checksum 與 `FileSystemRevision`。
- **讀取**：boot sector 參數（扇區/叢集大小、FAT 偏移、叢集堆積偏移、根目錄叢集）；
  FAT 鏈；`NoFatChain` 連續旗標；目錄項目集合（0x85 File、0xC0 Stream、0xC1 Name），
  驗證 SetChecksum；Allocation Bitmap（0x81）；Up-case table（0x82）用於不分大小寫比對。
- **寫入**：配置位元圖與 FAT 更新、目錄項目集合建立/刪除、擴充/截斷、`mkdir`、`rename`。
  先寫資料、再寫 FAT/位元圖、最後寫目錄項目（崩潰時最多洩漏叢集）。
- **限制**：exFAT 沒有 journal、沒有 POSIX 權限與 symlink；掛載時用 mount option
  給整個卷冊一組固定 uid/mode。
- **dirty 旗標**：掛載時設 `VolumeDirty`，乾淨卸載時清除；掛載到已 dirty 的卷冊
  預設唯讀。
- **估計規模**：約 1500–2000 行。

### 階段 2：ext4

- **識別**：superblock（偏移 1024）魔數 `0xEF53`；檢查 `feature_incompat`，
  遇到不認得的旗標拒絕掛載（`feature_ro_compat` 不認得則只讀掛載）。
- **唯讀核心**：block group descriptor（含 64bit 與 `meta_bg`）、inode 表、
  **extent 樹**（取代舊的間接區塊；仍要支援 indirect 以讀舊卷冊）、
  `filetype` 目錄項目、線性目錄；`htree` 目錄先用線性掃描讀（可讀，只是慢）。
  `inline_data`、`encrypt`、`casefold` 初期拒絕。
- **journal（jbd2）**：
  - 唯讀掛載若 journal 非空 → 拒絕或重播到記憶體（先拒絕）。
  - 實作 **journal replay**（掛載時）才算可安全寫入。
  - 寫入使用 ordered 模式：資料先落盤，metadata 走 journal。
- **寫入**：block bitmap / inode bitmap 配置、extent 插入與分裂、目錄項目新增/刪除、
  `unlink`、`rename`、`truncate`、metadata checksum（`metadata_csum`，crc32c）。
- **POSIX 語意**：uid/gid/mode、時間戳（ns 擴充）、hard link、symlink（快速與一般）。
  這些需要 `vfs::Stat` 擴充（階段 0.5）。
- **不做**：quota、ACL/xattr（只讀取忽略）、加密、verity、bigalloc（初期拒絕）。
- **估計規模**：唯讀約 2000 行；加寫入與 journal 約 +3000 行。

### 階段 3：NTFS

- **識別**：boot sector 偏移 3 的 `"NTFS    "`。
- **唯讀**：MFT 與 `$MFT` 自己的 run list、fixup（update sequence array）、
  屬性解析（`$STANDARD_INFORMATION`、`$FILE_NAME`、`$DATA`、`$INDEX_ROOT`、
  `$INDEX_ALLOCATION`、`$BITMAP`、`$ATTRIBUTE_LIST`）、resident / non-resident、
  run list 解碼、目錄索引 B+ 樹走訪、sparse 檔案、UTF-16 檔名、不分大小寫比對（`$UpCase`）。
  讀取 hard link 與 junction（reparse point）當作符號連結目標資訊但不跟隨，先忽略。
- **dirty 處理**：`$Volume` 的 dirty 旗標或 `$LogFile` 非乾淨 → 預設拒絕讀寫掛載
  （Windows 快速啟動留下的休眠狀態很常見，必須明確提示使用者）。
- **寫入（後期，可選）**：只支援覆寫既有檔案內容（不改長度）、再做建立/刪除。
  需要 `$MFT` 位元圖、`$Bitmap`、索引樹插入/分裂、`$LogFile` 處理。風險最高，
  建議在 ext4 穩定、有 fuzz 測試之後才決定要不要做。
- **不做**：壓縮、EFS 加密、ADS 寫入（讀取 ADS 之後再說）、ACL（`$Secure` 只讀）。
- **估計規模**：唯讀約 2500 行；寫入另算。

## 4. 目錄結構 / Layout

```text
kernel/io/block_cache.cpp   階段 0
kernel/fs/partition.cpp
kernel/fs/probe.cpp
kernel/text/utf.cpp
kernel/fs/exfat/{exfat.cpp, exfat_vfs.cpp, README.md}
kernel/fs/ext4/{ext4.cpp, extent.cpp, journal.cpp, ext4_vfs.cpp, README.md}
kernel/fs/ntfs/{ntfs.cpp, mft.cpp, index.cpp, ntfs_vfs.cpp, README.md}
include/shirley/{block_cache,partition,exfat,ext4,ntfs}.hpp
drivers/block/virtio_blk.cpp   (M5)
tests/{exfat,ext4,ntfs}_smoke.cpp
tests/images/                  (不入版控，由腳本產生)
```

每個格式沿用 SHRFS 的慣例：核心邏輯一個檔案、VFS 轉接一個檔案、逐位元組小端解碼，
避免依賴結構排列；`mount()` 驗證完所有關鍵結構，損壞的卷冊在掛載時就失敗。

## 5. 測試策略 / Testing

- **映像產生**（主機端腳本 `tools/make-test-images.sh`，需要 Linux/WSL）：
  `mkfs.exfat`、`mkfs.ext4 -O ^has_journal / 預設`、`mkntfs`，用 `mount` 或
  `debugfs`/`ntfs-3g` 放入固定內容（含長檔名、中文檔名、大檔案、稀疏檔、深目錄）。
  以 hash 驗證讀出內容。
- **host smoke tests**：`RamDisk` 載入映像，跑 `lookup/list/read`。
- **QEMU 整合測試**：virtio-blk 掛上映像，開機後 shell 跑 `mount` 與 `cat`。
- **寫入驗證**：核心寫完後在主機端用 `fsck.exfat` / `e2fsck -fn` / `ntfsfix --no-action`
  檢查，這是寫入正確性的最終標準。
- **損壞與 fuzz**：對每種映像做位元翻轉，要求 `mount()` 回傳 false 或讀取回傳錯誤，
  不得越界或無窮迴圈（FAT 鏈、extent 樹、MFT 都要有走訪次數上限與環偵測）。
- **互通測試**：在 ShirleyOS 寫的卷冊要能被 Windows/Linux 正確讀取，反之亦然。

## 6. 風險與決策 / Risks and decisions

- **資料損毀**：寫入支援一律先唯讀上線；寫入需通過 fsck 往返測試才打開。
  掛載預設唯讀，`mount -w` 才可寫。
- **授權**：從規格獨立實作，**不複製** Linux/ntfs-3g/exfatprogs 的程式碼（GPL）。
  參考公開規格：Microsoft exFAT 規格、ext4 kernel.org 文件、NTFS 逆向文件
  （Linux-NTFS 專案文件）。exFAT 專利已由 Microsoft 開放授權（OIN），仍建議保留此備註。
- **記憶體**：快取大小要有上限，無堆積時以固定頁池起步。
- **並行**：目前一次只有一個行程，快取不加鎖；M2 搶佔式排程之前要重新檢視。
- **時間**：沒有 RTC 驅動時時間戳寫 0 或固定值，並在文件註明。
- **Windows 開發環境**：本機是 Windows，產生/檢查映像需 WSL 或 Docker。

## 7. 里程碑 / Milestones

| 里程碑 | 內容 | 前置 |
| --- | --- | --- |
| FS-0 | 階段 0.1–0.4、0.7：堆積、快取、分割表、virtio-blk、`mount` 指令 | M5 virtio |
| FS-1 | exFAT 唯讀 | FS-0 |
| FS-2 | VFS 寫入擴充（0.5、0.6、0.8）+ exFAT 讀寫，通過 fsck.exfat | FS-1 |
| FS-3 | ext4 唯讀（extent、htree 線性） | FS-0 |
| FS-4 | ext4 journal replay + 讀寫，通過 e2fsck | FS-2、FS-3 |
| FS-5 | NTFS 唯讀 | FS-0 |
| FS-6 | NTFS 寫入（可選） | FS-4、FS-5 |

FS-1/FS-3/FS-5 可在 FS-0 完成後並行。依規格書〈規格維護〉，每個里程碑完成時要
同步更新 `OS_SPEC.md`（中英）與 `ROADMAP.md`。

## 8. 立即下一步 / Immediate next steps

1. 在 `OS_SPEC.md` 的後續規劃加入 FS-0…FS-6，並與 M4/M5 對齊。
2. 實作核心堆積與區塊快取（0.1、0.2），用現有 `RamDisk` 與 SHRFS 驗證快取不改變行為。
3. 寫 `partition.cpp` 與 host smoke test（用手工 MBR/GPT 位元組）。
4. 寫 `tools/make-test-images.sh`，先產出 exFAT 映像。
5. 開始 exFAT 唯讀驅動。
