menu "File Systems"

menu "Basic"

config SYNO_FS_STAT
	bool "SYNOStat"
	default y
	depends on SYNO_SYSTEM_CALL

config SYNO_FS_XATTR
	bool "Synology extended attribute namespace"
	default y

config SYNO_FS_ARCHIVE_BIT
	bool "Archive bit"
	default y
	depends on SYNO_FS_XATTR

config SYNO_FS_ARCHIVE_VERSION
	bool "syno archive version"
	default y

config SYNO_FS_WINACL
	bool "Synology WinACL"
	default y
	select SYNO_FS_ARCHIVE_BIT

config SYNO_FS_RELATIME_PERIOD
	bool "Add mount option to set update period of relatime"
	default y

config SYNO_FS_RECVFILE
	bool "Support syno_recv_file syscall"
	default y

config SYNO_FS_CASELESS_STAT
	bool "Support caseless stat in filesystem "
	default y
	depends on SYNO_SYSTEM_CALL

config SYNO_FS_LOCKER
	bool "Mechanism to lock/unlock data for WORM purpose"
	default y

config SYNO_FS_CREATE_TIME
	bool "syno create time"
	default y

config SYNO_FS_SYNOTIFY
	bool "Support Synotify"
	default y
	depends on FSNOTIFY && SYNO_SYSTEM_CALL

config SYNO_FS_SYNOBOOT_LOG
	bool "Log for mount/unmount synoboot"
	default y

config SYNO_FS_UNMOUNT
	bool "syno unmount dump opened file"
	default y

config SYNO_FS_AGGREGATE_RECVFILE
	bool "Enable syno_recv_file syscall to use aggregate_write_end()"
	default y
	depends on SYNO_FS_RECVFILE

config SYNO_FS_QUOTA_QUERY
	bool "Support query quota used and limit in vfs operations"
	default y
	depends on SYNO_BTRFS_SYNO_QUOTA

config SYNO_FS_SPACE_USAGE
	bool "Support query space usage in vfs operations"
	default y

config SYNO_FS_DEV
	bool "SYNO FS dev support"
	default y
	depends on SWAP && SYNO_FEATURES

config SYNO_FS_RBD_META
	tristate "Syno rbd meta support"
	default y
	depends on SWAP && SYNO_FEATURES

config SYNO_FS_ROOT_PRJQUOTA
	bool "Project quota restricts root usage"
	default y
	depends on QUOTA && SYNO_FEATURES

config SYNO_FS_SHOW_INCOMPAT_SUPP
	bool "Show file system's incompatible support flags"
	default y

config SYNO_FS_SHOW_COMPAT_RO_SUPP
	bool "Show file system's compatible read-only support flags"
	default y

config SYNO_FS_GENERIC_FIEMAP_FOR_KERNEL_SPACE
	bool "Add generic fiemap for kernel space"
	default y

config SYNO_FS_SPLICE_FSNOTIFY
	bool "splice: report related fsnotify events"
	default y

endmenu #Basic

menu "CIFS"
config SYNO_CIFS_CREATE_TIME
	bool "CIFS syno create time"
	default y
	depends on CIFS && SYNO_FS_STAT && SYNO_FS_CREATE_TIME

config SYNO_CIFS_REPLACE_NATIVE_OS
	bool "Identify Synology CIFS mount"
	default y

config SYNO_CIFS_TCON_RECONNECT_CODEPAGE_UTF8
	bool "Cifs with UTF8 code page"
	default y

config SYNO_CIFS_INIT_NLINK
	bool "Initialize fattr with cf_nlink=1 in cifs_dir_info_to_fattr"
	default y

config SYNO_CIFS_MOUNT_CASELESS
	bool "Caseless remote mount"
	default y

config SYNO_CIFS_FORCE_UMOUNT
	bool ""
	default y

config SYNO_CIFS_COVERITY
	bool "fix cifs coverity"
	default y

config SYNO_CIFS_SMB_OPS
	bool "add vers=syno for switch SMB1~3 from negotiate"
	default y
	depends on SYNO_CIFS_REPLACE_NATIVE_OS

config SYNO_CIFS_RECONNECT
	bool "modify cifs reconnect behavior to prevent wait mutex lock cause hung task"
	default y

config SYNO_CIFS_FIX_BUGS
	bool "fix open source bugs. It might be fix in the future kernel release."
	default y

endmenu #CIFS

menu "FAT"

config SYNO_FAT_CREATE_TIME
	bool "FAT syno create time"
	default y
	depends on FAT_FS && SYNO_FS_STAT && SYNO_FS_CREATE_TIME

config SYNO_FAT_DEFAULT_MNT_FLUSH
	bool "Set FAT default mount option 'flush'"
	default y
	depends on FAT_FS

config SYNO_FAT_SKIP_WAITING_TIME_WHEN_CLOSE_FILE
	bool "Skip waiting time when file close"
	default y
	depends on FAT_FS && SYNO_FAT_DEFAULT_MNT_FLUSH

endmenu #FAT

menu "EXT3"

config SYNO_EXT3_ARCHIVE_BIT
	bool "Ext3 syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && EXT3_FS

config SYNO_EXT3_ARCHIVE_VERSION
	bool "Ext3 syno archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && EXT3_FS && SYNO_EXT4_XATTR

config SYNO_EXT3_CREATE_TIME
	bool "Ext3 syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && EXT3_FS

endmenu #EXT3

menu "EXT4"

config SYNO_EXT4_LAZYINIT_INFO
	bool "Export lazyinit progress to sysfs"
	default y
	depends on EXT4_FS

config SYNO_EXT4_LAZYINIT_DYNAMIC_SPEED
	bool "Adjust lazyinit speed dynamically"
	default y
	depends on EXT4_FS

config SYNO_EXT4_LAZYINIT_WAIT_MULT
	int "Number of lazyinit wait multiplier"
	default 2
	depends on EXT4_FS

config SYNO_EXT4_XATTR
	bool "Ext4 syno xattr"
	default y
	depends on SYNO_FS_XATTR && EXT4_FS

config SYNO_EXT4_STAT
	bool "Ext4 SYNOStat"
	default y
	depends on SYNO_FS_STAT && EXT4_FS

config SYNO_EXT4_ARCHIVE_BIT
	bool "Ext4 syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && EXT4_FS

config SYNO_EXT4_ARCHIVE_VERSION
	bool "Ext4 syno archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && EXT4_FS && SYNO_EXT4_XATTR

config SYNO_EXT4_WINACL
	bool "Enable Synology WinACL in Ext4"
	default y
	depends on EXT4_FS && SYNO_FS_WINACL
	select SYNO_EXT4_ARCHIVE_BIT

config SYNO_EXT4_CREATE_TIME
	bool "Ext4 syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && EXT4_FS

config SYNO_EXT4_SYMLINK_IOCTL
	bool "add ioctl to symbolic link"
	default y
	depends on EXT4_FS

config SYNO_EXT4_CASELESS_STAT
	bool "Support caseless stat in ext4"
	default y
	depends on SYNO_FS_CASELESS_STAT && EXT4_FS

config SYNO_EXT4_ERROR_REPORT
	bool "Enable ext4 error report mechanism"
	default y
	depends on EXT4_FS

config SYNO_EXT4_INODE_NUM_OVERFLOW_FIX
	bool "Fix ext4 inode number overflow problem on large volume (>64TB)"
	default y
	depends on EXT4_FS && 64BIT

config SYNO_EXT4_DEFAULT_MNTOPT_JOURNAL_CKSUM
	bool "Ext4 set default mount option journal_checksum"
	default y
	depends on EXT4_FS

config SYNO_EXT4_UNUSED_HINT
	bool "FIHINTUNUSED ioctl to send free space information to underly layers"
	default y
	depends on EXT4_FS && SYNO_MD_UNUSED_HINT

config SYNO_EXT4_DISABLE_INODES_COUNT_CHECK
	bool "disable mount time check about inodes count"
	default y
	depends on EXT4_FS

config SYNO_EXT4_ALLOW_MORE_RESERVED_GDT_BLOCKS
	bool "Increase reserved GDT to break the 16TB boundary of online resize"
	default y
	depends on EXT4_FS

config SYNO_EXT4_CAPABILITY_FLAGS
	bool "synology capability flags"
	default y
	depends on EXT4_FS

config SYNO_EXT4_RBD_META
	bool "Reserve SynoRBD meta in filesystem"
	default y
	depends on EXT4_FS && SYNO_FS_RBD_META && SYNO_EXT4_CAPABILITY_FLAGS

config SYNO_EXT4_SYNOOPT
	bool "Synology specific mount options"
	default y
	depends on EXT4_FS

config SYNO_EXT4_ROOT_PRJQUOTA
	bool "Project quota restricts root usage on EXT4"
	default y
	depends on QUOTA && SYNO_FEATURES && SYNO_FS_ROOT_PRJQUOTA && SYNO_EXT4_SYNOOPT

config SYNO_EXT4_SKIP_UNNECESSARY_BARRIER
	bool "Skip blk flush with data!=writeback when sync_fs"
	default y
	depends on EXT4_FS

config SYNO_EXT4_BH_FLAGS_WARNING
	bool "Workaround for incorrect bh flags"
	default y
	depends on EXT4_FS

config SYNO_EXT4_FIX_PADDING_ZERO
	bool "Fix padding zero issues after crash"
	default y
	depends on EXT4_FS

endmenu #EXT4

menu "BTRFS"

config SYNO_BTRFS_XATTR
	bool "Btrfs syno xattr"
	default y
	depends on SYNO_FS_XATTR && BTRFS_FS

config SYNO_BTRFS_STAT
	bool "Btrfs SYNOStat"
	default y
	depends on SYNO_FS_STAT && BTRFS_FS

config SYNO_BTRFS_ARCHIVE_BIT
	bool "Btrfs syno archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && BTRFS_FS && SYNO_BTRFS_XATTR

config SYNO_BTRFS_ARCHIVE_VERSION
	bool "Support syno archive version for btrfs"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && SYNO_BTRFS_XATTR && BTRFS_FS

config SYNO_BTRFS_WINACL
	bool "Enable Synology WinACL in Btrfs"
	default y
	depends on BTRFS_FS && SYNO_FS_WINACL
	select SYNO_BTRFS_ARCHIVE_BIT
	select SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT

config SYNO_BTRFS_CREATE_TIME
	bool "Add syno create time for btrfs"
	default y
	depends on SYNO_FS_CREATE_TIME && SYNO_BTRFS_XATTR && BTRFS_FS

config SYNO_BTRFS_RESIZE_QUERY
	bool "Add a dry-run mode in BTRFS_IOC_RESIZE"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_METADATA_RESERVE
	bool "reserve metadata chunk with metadata_ratio"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FALLOCATE_MARK_WRITTEN
	bool "Mark fallocated area as written rether than prealloc"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_VFS_INO_TO_PATH
	bool "Query ino to path in vfs"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QGROUP_QUERY
	bool "Add ioctl for btrfs qgroup query"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DELAYED_ORPHAN_CLEANUP_WHEN_MOUNT
	bool "improve mount time with dealyed orphan cleanup for tree-root"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT
	bool "Btrfs mount option expand to 64-bit"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPAT_RO_NO_REMOUNT_RW
	bool "No remount as read-write is allowed for compat-ro"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MERGE_HOLES
	bool "file hole can be merged with both previous and next hole items"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SUBVOL_CREATE_TIME
	bool "Support btrfs send subvolume create time"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_READONLY_SUBVOL_RUUID_SET
	bool "Set btrfs read-only subvol ruuid"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RENAME_READONLY_SUBVOL
	bool "Allow btrfs to rename read-only subvolume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RECLAIM_SPACE
	bool "add support for reclaim space from partial used extents"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT

config SYNO_BTRFS_IOC_SYNC_SYNO
	bool "Add a new ioctl BTRFS_IOC_SYNC_SYNO for iscsi"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLONE_RANGE_V2
	bool "clone range v2 version"
	default y
	depends on BTRFS_FS && SYNO_EXPORT_SYMBOL

config SYNO_BTRFS_DEFAULT_SAPCE_CACHE_V2
	bool "default space cache v2"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SUBVOL_FLAG
	bool "add btrfs send subvol flag"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_FLAGS_SUPPORT
	bool "Support syno btrfs send flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_SKIP_FIND_CLONE
	bool "add a send flag to skip find_extent_clone process"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_FALLBACK_COMPRESSION
	bool "add a send flag to convert file compression algorithm from zstd to lzo"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_FALLOCATE_SUPPORT
	bool "Support fallocate cmd to pre-allocate file extents while sending subvols"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_CALCULATE_TOTAL_DATA_SIZE
	bool "add btrfs calculate send data size"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SEND_FLAGS_SUPPORT

config SYNO_BTRFS_SEND_SUPPORT_PAUSE_RESUME
	bool "add btrfs send support pause/resume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CASELESS_STAT
	bool "Add syno caseless stat for btrfs"
	default y
	depends on SYNO_FS_CASELESS_STAT && BTRFS_FS

config SYNO_BTRFS_LOCKER
	bool "Mechanism to lock/unlock data for WORM purpose"
	default y
	depends on BTRFS_FS && SYNO_FS_LOCKER && SYNO_BTRFS_FEATURE_TREE

config SYNO_BTRFS_LOCKER_SNAPSHOT
	bool "Mechanism to lock/unlock read-only snapshot"
	default y
	depends on SYNO_BTRFS_LOCKER

config SYNO_BTRFS_LOCKER_SUBVOLUME_CLOCK
	bool "Support subvolume clock for locker"
	default y
	depends on SYNO_BTRFS_LOCKER

config SYNO_BTRFS_REMOVE_FLAG_TREE_CHECK
	bool "remove tree checker machanism for inode item flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IGNORE_PRE_WRITE_TREE_CHECK
	bool "remove tree checker machanism for pre-write io"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ALLOW_SNAPSHOT_DELETE_STOP
	bool "Support btrfs cleaner stop deleting snapshot"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SUBVOLUME_HIDE
	bool "Support subvolume hide flag"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BLOCK_GROUP_HINT_TREE
	bool "Add a block group hint tree to speedup volume mount."
	default y
	depends on BTRFS_FS
	select SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT

config SYNO_BTRFS_LOG_TREE_RSV_METADATA
	bool "reserve metadata group for log tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLUSTER_ALLOCATION
	bool "Cluster allocation"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BLOCK_GROUP_CACHE_TREE
	bool "Add a block group cache tree to speedup volume mount."
	default y
	depends on BTRFS_FS
	select SYNO_BTRFS_MOUNT_OPTION_EXPAND_64BIT

config SYNO_BTRFS_LOG_TREE_USE_SINGLE_METADATA
	bool "log tree use single metadata instead of dup"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPR_DEFAULT_SETTING
	bool "Apply default setting of syno compression"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_JOURNAL_INFO_BUG
	bool "fix btrfs journal_info bug"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DATA_CHUNK_ALLOCATE_TOO_MUCH_FOR_PARALLEL_WRITE
	bool "fix data chunk allocate too much for parallel write"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_FIEMAP_RESULT_NOT_CORRECTED
	bool "fix fiemap result not corrected"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FREE_SPACE_ANALYZE
	bool "A new ioctl to analyze btrfs free space"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FEATURE_METADATA_CACHE
	bool "add metadata cache feature with ssd cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_TRIM_SYS_CHUNK
	bool "Avoid trim system chunk."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FREE_EXTENT_MAPS
	bool "Add a machanisim to drop extent map cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_TUNE_DEFAULT_MAX_INLINE_SIZE
	bool "tune default max inline size"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SCRUB_CANCEL
	bool "Let btrfs cancel scrubbing faster"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMPR_CTL
	bool "Operate compressed files"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SYSFS_BLOCK_GROUP_CNT
	bool "add sysfs interface about block group count information"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_COMMIT_STATS
	bool "Btrfs performance stats about commit transaction."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SUPPORT_FULLY_CLONE_BETWEEN_CSUM_AND_NOCSUM_DIR
	bool "Fix cp --reflink failed between csum/nocsum"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DISABLE_CLONE_BETWEEN_COMPR_AND_NOCOMPR_DIR
	bool "Prevent clone files between compress/nocompress share folders"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SKIP_BLOCK_GROUP
	bool "add mount option to skip block group"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SNAPSHOT_SIZE_CALCULATION
	bool "add ioctl to calculate actual disk size of snapshots"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_UNUSED_HINT
	bool "FIHINTUNUSED ioctl to send free space information to underly layers"
	default y
	depends on SYNO_MD_UNUSED_HINT
	depends on BTRFS_FS

config SYNO_BTRFS_SYSFS_FREE_SPACE_TREE
	bool "add sysfs interface about free space tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_PERF_STATS
	bool "Btrfs performance stats"
	default y
	  depends on BTRFS_FS && DEBUG_FS

config SYNO_BTRFS_FIX_INCREMENTAL_SEND
	bool "fix btrfs send incremental send"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FEATURE_SPACE_USAGE
	bool "add space usage for different subvolume type"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DATA_CORRECTION
	bool "Report btrfs data checksum failure"
	default n
	depends on BTRFS_FS && SYNO_DATA_CORRECTION

config SYNO_BTRFS_SEND_SIGNAL_HANDLE
	bool "add signal handling for exiting btrfs send quickly"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SYNO_QUOTA
	bool "Syno btrfs quota 2.0"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_GLOBAL_RESERVE_MINIMAL_VALUE
	bool "Keep btrfs global reserve more than 256MB if the fs is larger than 10G."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_REFILL_GLOBAL_RSV
	bool "Add retry global rsv fill when alloc metadata block except BTRFS_BLOCK_RSV_TEMP"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DROP_LOG_TREE
	bool "add mount option to drop log tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_MULTIPLE_WRITEBACK
	bool "improve grantley random write performance."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_DROP_PROGRESS_INCONSISTENT
	bool "fix drop progress inconsistent when drop snapshot error"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG
	bool "Add syno_flag to file extent"
	default n
	depends on BTRFS_FS && SYNO_BTRFS_REMOVE_FLAG_TREE_CHECK

config SYNO_BTRFS_DEDUPE
	bool "Synology btrfs dedupe"
	default n
	depends on BTRFS_FS && SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG && SYNO_BTRFS_RECLAIM_SPACE && SYNO_BTRFS_SYNO_QUOTA

config SYNO_BTRFS_COW_ASYNC_THROTTLE
	bool "enhance latency for cow with async throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIMIT_WRITE_BIO_SIZE
	bool "Btrfs limit bio size max 64k for latency"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ORDERED_EXTENT_THROTTLE
	bool "avoid OOM with throttle for ordered extent"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_PRIORITY_ORDERED_EXTENT
	bool "improve latency with move work to high workqueue for ordered extent"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_UNLOCKED_BUFFER_WRITE
	bool "Btrfs: implement unlocked buffered write"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_BALANCE_DRY_RUN
	bool "add btrfs balance dry run"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FEATURE_TREE
	bool "synology feature tree"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CAPABILITY_FLAGS
	bool "synology capability flags"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_RBD_META
	bool "Reserve SynoRBD meta in filesystem"
	default y
	depends on BTRFS_FS && SYNO_FS_RBD_META && SYNO_BTRFS_FEATURE_TREE && SYNO_BTRFS_CAPABILITY_FLAGS

config SYNO_BTRFS_ASYNC_METADATA_RECLAIM
	bool "improve latency with async metadata reclaim"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ASYNC_DATA_FLUSH
	bool "improve latency with async flush"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_MULTIPLE_WRITEBACK && SYNO_BTRFS_ASYNC_METADATA_RECLAIM

config SYNO_BTRFS_ASYNC_METADATA_FLUSH_AND_THROTTLE
	bool "improve latency with async metadata flush and throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_VERIFY_DEV_EXTENTS_WITH_READAHEAD_FORWARD_ALWAYS
	bool "use readahead forward always for verify dev extents"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DELAYED_REF_THROTTLE
	bool "Add btrfs delayed ref throttle"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CLEANER_THROTTLE
	bool "Throttle cleaner"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_DELAYED_REF_THROTTLE

config SYNO_BTRFS_SEND_DONOT_SKIP_PRECESSING_BACKREFERENCE
	bool "to provide consistent command count for pause"
	default y
	depends on SYNO_BTRFS_SEND_SUPPORT_PAUSE_RESUME

config SYNO_BTRFS_FIX_PARTIAL_WRITE_END_DEADLOCK
	bool "fix partial write end deadlock"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_TRIM_ENOSPC
	bool "Fix trim will lead to ENOSPC and lose data."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIST_HARDLINKS
	bool "list hardlinks with inum"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_BUG_WEHN_QGROUP_ATOMIC_ALLOC_FAILED
	bool "Fix bug when qgroup ulist_node atomic alloc failed."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SKIP_CLEAR_EXTENT_DEFRAG_WHEN_NOCOW_ORDERED_EXTENT
	bool "Skip clear extent defrag when nocow ordered extent"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_CLONERANGE_NBYTES_WRONG
	bool "fix clonerange nbytes wrogn"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_ALLOCATOR
	bool "synology btrfs allocator"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SKIP_RESERVE_WHEN_NO_QUOTA_LIMIT
	bool "synology btrfs skip quota reserve when no quota limit"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_SYNO_QUOTA

config SYNO_BTRFS_NON_BLOCKING_PUNCH_HOLE
	bool "Add btrfs non_blocking_punch_hole"
	default y
	depends on BTRFS_FS && SYNO_BTRFS_DELAYED_REF_THROTTLE

config SYNO_BTRFS_MOUNT_STATS
	bool "Btrfs performance stats about mount."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_UUID_CHECKING
	bool "Fix unnecessary uuid rescan"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_UNNECESSARY_FLUSH_WITH_OLD_SIZE_IS_ZERO_WHEN_TRUNCATE
	bool "Fix unnecessary flush with old size is zero when truncate"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_LIMIT_PRE_RUN_DELAYED_REFS_FOR_COMMIT_TRANSACTION
	bool "Limit pre-run delayed-refs for commit transaction"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_FIEMAP_FOR_LARGE_SPARSE_FILE
	bool "Fiemap improve for large sparse file"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_HIBERNATION_MONITOR
	bool "Monitor modified log for hibernation"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_CHUNK_LOGICAL_OVERFLOW
	bool "fix chunk logical overflow"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_STATISTICS
	bool "add meta statistics"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QUOTA_SOFT_LIMIT
	bool "add quota soft limit"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_SEND_IMPROVE_CHECK_NEW_DIR_CREATED_WITH_NEW_DIR_CACHE
	bool "improve check new dir created with new dir cache"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AUTO_DISABLE_COMPRESS_WHEN_NOCOW_SET
	bool "auto disable compress when nocow set"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_QUICK_BALANCE
	bool "balance only one block group"
	default y
	depends on SYNO_BTRFS_ALLOCATOR

config SYNO_BTRFS_SEARCH_BY_EXTENT_TYPE
	bool "search extent item by extent type"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_CHECK_INTEGRITY
	bool "Auto fix error in check_leaf()."
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FREE_SPACE_CACHE_IMPROVEMENT
	bool "free space cache improvement"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_DELETE_SNAPSHOT_CHECK
	bool "delete snapshot should check whether it is snapshoted by someone"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_AVOID_ORPHANED_RUUID
	bool "do not set ruuid on orphaned subvolume"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_IMPROVE_NOCOW_WRITE_WHEN_VOLUME_FULL
	bool "improve nocow write when volume full for iSCSI thick lun"
	default y
	depends on BTRFS_FS

config SYNO_BTRFS_FIX_XATTR_EXTENSION_ENOSPC
	bool "fix xattr extension enospc"
	default y
	depends on BTRFS_FS

endmenu #BTRFS

menu "ECRYPT"

config SYNO_ECRYPTFS_ARCHIVE_BIT
	bool "Ecryptfs archive bit"
	default y
	depends on SYNO_FS_ARCHIVE_BIT && ECRYPT_FS

config SYNO_ECRYPTFS_FILENAME_SYSCALL
	bool "System calls to get encrypt or decrypt filename"
	default y
	depends on SYNO_SYSTEM_CALL && ECRYPT_FS

config SYNO_ECRYPTFS_AVOID_MOUNT_REPEATLY
	bool "Avoid ecryptfs mount repeatly at the same mount point"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_REMOVE_TRUNCATE_WRITE
	bool "Speed up ecryptfs truncate by skipping zeros write"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_CHECK_SYMLINK_LENGTH
	bool "Check ecryptfs symlink target length after encryption"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_FALLOCATE_SUPPORT
	bool "Add fallocate for eCryptfs"
	default y
	depends on ECRYPT_FS && SYNO_ECRYPTFS_REMOVE_TRUNCATE_WRITE

config SYNO_ECRYPTFS_FAST_LOOKUP
	bool "Fast lookup, read i_size from xattr"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_PASS_BTRFS_IOCTL
	bool "Pass syno btrfs ioctl to lower btrfs filesystem"
	default y
	depends on ECRYPT_FS && BTRFS_FS

config SYNO_ECRYPTFS_ARCHIVE_VERSION
	bool "Ecryptfs archive version"
	default y
	depends on SYNO_FS_ARCHIVE_VERSION && ECRYPT_FS

config SYNO_ECRYPTFS_CREATE_TIME
	bool "Ecryptfs syno create time"
	default y
	depends on SYNO_FS_CREATE_TIME && ECRYPT_FS

config SYNO_ECRYPTFS_STAT
	bool "Ecryptfs SYNOStat"
	default y
	depends on SYNO_FS_STAT && ECRYPT_FS

config SYNO_ECRYPTFS_LOWER_INIT
	bool "Ecryptfs always initial lower file with rw, ignore security check on initialization"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_SKIP_EQUAL_ISIZE_UPDATE
	bool "Update ecryptfs i_size only when they are different"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_SKIP_EDQUOT_WARNING
	bool "Ecryptfs skip EDQUOT, ENOSPC warning log"
	default y
	depends on ECRYPT_FS

config SYNO_ECRYPTFS_WINACL
	bool "Enable syno acl in ecryptfs"
	default y
	depends on ECRYPT_FS && SYNO_FS_WINACL

config SYNO_ECRYPTFS_EXPORT
	bool "Enable ecryptfs nfs export"
	default y
	depends on ECRYPT_FS && EXPORTFS

config SYNO_ECRYPTFS_REDUCE_MEMCPY
	bool "Reduce one memcpy on ecryptfs for performance."
	default y
	depends on ECRYPT_FS && BTRFS_FS && SYNO_FS_AGGREGATE_RECVFILE

config SYNO_ECRYPTFS_DISABLE_READAHEAD
	bool "disable readahead with bdi"
	default y
	depends on ECRYPT_FS

endmenu #ECRYPT

menu "NFS"

config SYNO_NFSD_AVOID_HUNG_TASK_WHEN_UNLINK_BIG_FILE
	bool "Avoid parent mutex hung task when unlink big file"
	default y
	depends on NFSD

config SYNO_NFSD_HIDDEN_FILE
	bool "Hide system directories"
	default y
	depends on NFSD

config SYNO_NFSD_UDP_PACKET
	bool "Provide a interface for user to set the udp packet size they want"
	default y
	depends on NFSD

config SYNO_NFSD_UDP_MAX_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default 32768
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_UDP_MIN_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default	4096
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_UDP_DEF_PACKET_SIZE
	int "Provide a interface for user to set the udp packet size they want"
	default 8192
	depends on SYNO_NFSD_UDP_PACKET

config SYNO_NFSD_SQUASH_TO_ADMIN
	bool "Grant permission of administrators group to admin user"
	default y
	depends on NFSD

config SYNO_NFSD_UNIX_PRI
	bool "Provide a interface for user to enable command chmod or not on ACL share"
	default y
	depends on NFSD && SYNO_FS_WINACL

config SYNO_NFSD_WINACL
	bool "Support WinACL in NFS"
	default y
	depends on NFSD && SYNO_FS_WINACL

config SYNO_NFSD_NUMA_SVC_POOL_PERNODE
	bool "Enhance NFS performance for numa model"
	default y
	depends on NFSD && NUMA

config SYNO_NFSD_LATENCY_REPORT
	bool "Add /proc/net/rpc/nfsd_lat to monitor nfsd latency"
	default y
	depends on NFSD

config SYNO_NFS_VAAI_SUPPORT
	bool "NFS VAAI support"
	default y
	depends on NFSD

config SYNO_NFS_VAAI_LAZY_CLONE
	bool "NFS VAAI lazy clone support"
	default y
	depends on BTRFS_FS && SYNO_NFS_VAAI_SUPPORT

config SYNO_NFSD_SKIP_FINDING_IDLE_NFSD_IF_CONGESTED
	bool "Prevent nfsd from finding idle nfsd when all nfsd are busy."
	default y
	depends on NFSD

config SYNO_NFSD_INIT_NL4_SERVER_BY_NFS_OP
	bool "Prevent unnecessary initialization of the large arg 'struct nl4_server'."
	default y
	depends on NFSD

config SYNO_NFSD_SYNO_FILE_STATS
	bool "Add /proc/fs/nfsd/syno_file_stats to spot check file write"
	default y
	depends on NFSD

config SYNO_NFSD_CONNECTION_STAT
	bool "Mmonitor number of nfsd connection stat"
	default y
	depends on NFSD && SYNO_NFSD_LATENCY_REPORT

config SYNO_NFSD_UDC_COLLECTOR
	bool "Collect information for UDC"
	default y
	depends on NFSD && SYNO_NFSD_LATENCY_REPORT

config SYNO_NFSD_SHARE_QUOTA
	bool "Support share quota"
	default y
	depends on NFSD && SYNO_FS_QUOTA_QUERY

config SYNO_NFSD_TRACE
	bool "Tracepoint for debug and collecting data"
	default y
	depends on NFSD

config SYNO_NFSD_POOL_HINT
	bool "Hint RPC service to use the thread pool on the same node with the export path."
	default y
	depends on NFSD

endmenu #NFS

menu "HFSPLUS"

config SYNO_HFSPLUS_CREATE_TIME
	bool "HFS+ syno create time"
	default y
	depends on HFSPLUS_FS && SYNO_FS_STAT && SYNO_FS_CREATE_TIME

config SYNO_HFSPLUS_CASELESS
	bool "HFS+ shows caseless option for netatalk"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_NFC_WORKAROUND
	bool "workaround for reading on-disk NFC filename"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_EA
	bool "HFS+ enable EA support"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_BNODE_READ_PAGE_MAPPING_LIMIT
	bool "HFS+ page mapping limit"
	default y
	depends on HFSPLUS_FS

config SYNO_HFSPLUS_REMOVE_DEFAULT_CR_TYPE
	bool "remove default creator and type"
	default y
	depends on HFSPLUS_FS && SYNO_HFSPLUS_EA

endmenu #HFSPLUS

menu "UDF"

config SYNO_UDF_CASELESS
	bool "UDF use caseless lookup"
	default y
	depends on UDF_FS

endmenu #UDF

menu "FUSE"

config SYNO_FUSE_STAT
	bool "Support synostat and caseless stat by fuse"
	default y
	depends on FUSE_FS && SYNO_FS_STAT

config SYNO_FUSE_ARCHIVE_BIT
	bool "Support syno archive bit by fuse"
	default y
	depends on FUSE_FS && SYNO_FS_ARCHIVE_BIT

config SYNO_FUSE_ARCHIVE_VERSION
	bool "Support syno archive verion by fuse"
	default y
	depends on FUSE_FS && SYNO_FS_ARCHIVE_VERSION

config SYNO_FUSE_CREATE_TIME
	bool "Support syno create time by fuse"
	default y
	depends on FUSE_FS && SYNO_FS_CREATE_TIME

endmenu #FUSE

menu "OverlayFS"

config SYNO_OVERLAYFS_ALLOW_CUSTOMIZED_DENTRY_OPS
	bool "Workaround to allow case-insensitive upper and lower layers"
	default y
	depends on OVERLAY_FS

endmenu #OverlayFS

menu "AUFS"

config SYNO_AUFS_PATCH
	bool "AUFS patches for docker support"
	default y
	depends on AUFS_FS

config SYNO_AUFS_NO_AUTOGEN
	bool "Disable auto generated files"
	default y
	depends on AUFS_FS

endmenu #AUFS

menu "ConfigFS"

config SYNO_CONFIGFS_SIMPLE_ATTR_SIZE_AS_PAGE_SIZE
bool "set SIMPLE_ATTR_SIZE as PAGE_SIZE"
	default n
	depends on CONFIGFS_FS

endmenu #ConfigFS

menu "TMPFS"

config SYNO_TMPFS_CREATE_TIME
	bool "Tmpfs syno create time"
	default y
	depends on TMPFS && SYNO_FS_CREATE_TIME && SYNO_FS_STAT

config SYNO_TMPFS_ARCHIVE_BIT
	bool "Tmpfs syno archive bit"
	default y
	depends on TMPFS && SYNO_FS_ARCHIVE_BIT

endmenu #TMPFS

menu "ceph"

config SYNO_CEPH_RECVFILE
	bool "Support recvfile syscall on ceph"
	default y
	depends on CEPH_FS && SYNO_FS_RECVFILE

config SYNO_CEPH_STAT
	bool "ceph SYNOStat"
	default y
	depends on CEPH_FS && SYNO_FS_STAT

config SYNO_CEPH_CREATE_TIME
	bool "Let ceph support syno create time"
	default y
	depends on CEPH_FS && SYNO_FS_CREATE_TIME

config SYNO_CEPH_ARCHIVE_BIT
	bool "support syno archive bit"
	default y
	depends on CEPH_FS && SYNO_FS_ARCHIVE_BIT

config SYNO_CEPH_CASELESS_STAT
	bool "support syno caseless stat"
	default y
	depends on CEPH_FS && SYNO_FS_CASELESS_STAT

config SYNO_CEPH_WINACL
	bool "support syno acl"
	default y
	depends on CEPH_FS && SYNO_FS_WINACL

endmenu #ceph

endmenu #File Systems
