#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x86d28dcd, "module_layout" },
	{ 0x1640ef08, "per_cpu__current_task" },
	{ 0x525010b6, "__bread" },
	{ 0xb1233cc6, "save_mount_options" },
	{ 0x42216bb2, "generic_file_llseek" },
	{ 0xf0da70b0, "__mark_inode_dirty" },
	{ 0x2696c5de, "simple_link" },
	{ 0xd0d8621b, "strlen" },
	{ 0x8fde5204, "slab_buffer_size" },
	{ 0x815b5dd4, "match_octal" },
	{ 0x8915a228, "simple_sync_file" },
	{ 0x67053080, "current_kernel_time" },
	{ 0xd67ca71, "block_write_begin" },
	{ 0xc288f8ce, "malloc_sizes" },
	{ 0x327b2c4b, "simple_lookup" },
	{ 0x973873ab, "_spin_lock" },
	{ 0xfb8c95b0, "generic_file_aio_read" },
	{ 0x105e2727, "__tracepoint_kmalloc" },
	{ 0x44e9a829, "match_token" },
	{ 0x85df9b6c, "strsep" },
	{ 0x6f506293, "page_symlink_inode_operations" },
	{ 0xf3243bea, "generic_file_aio_write" },
	{ 0xfcccecda, "kunmap_atomic" },
	{ 0xdbebaae2, "kmap_atomic" },
	{ 0x1cf56ed6, "mpage_readpage" },
	{ 0xfc822e6e, "kmem_cache_alloc_notrace" },
	{ 0xd89da37f, "movable_zone" },
	{ 0xb72397d5, "printk" },
	{ 0x7c7ecbc6, "find_lock_page" },
	{ 0xcd3c0ef3, "d_alloc_root" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0xb1a78a91, "bdi_init" },
	{ 0x6c2e3320, "strncmp" },
	{ 0x627a1b9b, "page_symlink" },
	{ 0x8365f21a, "sync_dirty_buffer" },
	{ 0xb40b9da6, "unlock_page" },
	{ 0x6aff2a69, "__lock_page_killable" },
	{ 0x925ee715, "__brelse" },
	{ 0xc3122b1d, "contig_page_data" },
	{ 0xf1e5b68f, "simple_getattr" },
	{ 0x33c887a8, "simple_unlink" },
	{ 0x964313ca, "simple_dir_operations" },
	{ 0x26e83395, "generic_file_mmap" },
	{ 0xb12accbc, "block_write_full_page" },
	{ 0x54b2ebb3, "generic_write_end" },
	{ 0x3f898ad0, "do_sync_read" },
	{ 0x89572556, "kill_block_super" },
	{ 0x77b3d4b8, "alloc_buffer_head" },
	{ 0x1299b7ce, "generic_show_options" },
	{ 0xb2895c6f, "register_filesystem" },
	{ 0x5d0def34, "iput" },
	{ 0x37a0cba, "kfree" },
	{ 0x47207062, "do_sync_write" },
	{ 0x375de59, "generic_file_splice_write" },
	{ 0xde7918ca, "get_sb_bdev" },
	{ 0xc8d066, "put_page" },
	{ 0x6a26b965, "simple_statfs" },
	{ 0x3874229e, "mark_buffer_dirty" },
	{ 0xb4f98cb, "unregister_filesystem" },
	{ 0x3a3515ec, "init_special_inode" },
	{ 0xeb09de5b, "new_inode" },
	{ 0x95d693f4, "generic_file_splice_read" },
	{ 0x55b240a0, "simple_rename" },
	{ 0x30ff69d4, "clear_inode" },
	{ 0xbc6c3c35, "d_instantiate" },
	{ 0x4aadcb7c, "alloc_page_buffers" },
	{ 0x4eada069, "simple_rmdir" },
	{ 0xdf2b8f1d, "truncate_inode_pages" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "A3A3B04979559936E103C83");
