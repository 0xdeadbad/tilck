

#include <common_defs.h>
#include <string_util.h>
#include <hal.h>

#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>
#include <elf_loader.h>

#include <utils.h>
#include <tasklet.h>
#include <sync.h>

#include <fs/fat32.h>
#include <fs/exvfs.h>

#include <kb.h>
#include <timer.h>
#include <term.h>
#include <pageframe_allocator.h>
#include <multiboot.h>

extern u32 memsize_in_mb;
extern uptr ramdisk_paddr;
extern size_t ramdisk_size;
extern task_info *usermode_init_task;

/* Variables used by the cmdline parsing code */

static bool no_init;
static void (*self_test_to_run)(void);

/* -- */

void show_hello_message(void)
{
   printk("Hello from exOS! [%s build]\n", BUILDTYPE_STR);
}

void use_kernel_arg(int arg_num, const char *arg)
{
   printk("Kernel arg[%i]: '%s'\n", arg_num, arg);

   const size_t arg_len = strlen(arg);

   if (!strcmp(arg, "-noinit")) {
      no_init = true;
      return;
   }

   if (arg_len >= 3) {
      if (arg[0] == '-' && arg[1] == 's' && arg[2] == '=') {
         const char *a2 = arg + 3;
         char buf[256] = "selftest_";

         printk("Run selftest: '%s'\n", a2);

         memcpy(buf+strlen(buf), a2, strlen(a2) + 1);
         uptr addr = find_addr_of_symbol(buf);

         if (!addr) {
            panic("Self test function '%s' not found.\n", buf);
         }

         self_test_to_run = (void (*)(void)) addr;
         return;
      }
   }
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[256];
   char *dptr = buf;
   const char *ptr = cmdline;
   int args_count = 0;

   while (*ptr) {

      if (*ptr == ' ' || (dptr-buf >= (sptr)sizeof(buf)-1)) {
         *dptr = 0;
         dptr = buf;
         ptr++;
         use_kernel_arg(args_count++, buf);
         continue;
      }

      *dptr++ = *ptr++;
   }

   if (dptr != buf) {
      *dptr = 0;
      use_kernel_arg(args_count++, buf);
   }
}

void read_multiboot_info(u32 magic, u32 mbi_addr)
{
   if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
      return;

   multiboot_info_t *mbi = (void *)(uptr)mbi_addr;
   memsize_in_mb = (mbi->mem_upper)/1024 + 1;

   printk("*** Detected multiboot ***\n");

   if (mbi->flags & MULTIBOOT_INFO_MODS) {
      if (mbi->mods_count >= 1) {

         multiboot_module_t *mod = ((multiboot_module_t *)(uptr)mbi->mods_addr);
         ramdisk_paddr = mod->mod_start;
         ramdisk_size = mod->mod_end - mod->mod_start;

      } else {
         ramdisk_paddr = 0;
         ramdisk_size = 0;
      }
   }

   if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
      parse_kernel_cmdline((const char *)(uptr)mbi->cmdline);
   }
}

void show_additional_info(void)
{
   printk("TIMER_HZ: %i; MEM: %i MB\n",
           TIMER_HZ, get_phys_mem_mb());
}

void load_usermode_init()
{
   void *entry_point = NULL;
   void *stack_addr = NULL;
   page_directory_t *pdir = NULL;

   load_elf_program("/sbin/init", &pdir, &entry_point, &stack_addr);

   usermode_init_task =
      create_first_usermode_task(pdir, entry_point, stack_addr);

   printk("[load_usermode_init] Entry: %p\n", entry_point);
   printk("[load_usermode_init] Stack: %p\n", stack_addr);
}

void mount_ramdisk(void)
{
   if (!ramdisk_size) {
      printk("[WARNING] No RAMDISK found.\n");
      return;
   }

   printk("Mounting RAMDISK at PADDR %p...\n", ramdisk_paddr);
   filesystem *root_fs = fat_mount_ramdisk(KERNEL_PA_TO_VA(ramdisk_paddr));
   mountpoint_add(root_fs, "/");
}

void kmain(u32 multiboot_magic, u32 mbi_addr)
{
   term_init();
   show_hello_message();
   read_multiboot_info(multiboot_magic, mbi_addr);
   show_additional_info();

   setup_segmentation();
   setup_interrupt_handling();

   init_pageframe_allocator();

   init_paging();
   initialize_kmalloc();
   init_paging_cow();

   initialize_scheduler();
   initialize_tasklets();

   set_timer_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   DEBUG_CHECKED_SUCCESS(add_tasklet0(&init_kb));

   // TODO: make the kernel actually support the sysenter interface
   setup_sysenter_interface();

   mount_ramdisk();

   if (self_test_to_run)
      self_test_to_run();

   if (ramdisk_size && !no_init)
     load_usermode_init();

   printk("[kernel main] Starting the scheduler...\n");
   switch_to_idle_task_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
