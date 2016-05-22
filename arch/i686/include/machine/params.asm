%define PAGE_SIZE 4096
%define STACK_SZ  16384
%define KERN_BASE 0xC0000000
%define KPAGE_DIR_INDEX (KERN_BASE >> 22)
%define KERN_SIZE (0xFFFFFFFF-KERN_BASE)
%define UNUM_PAGETABS KPAGE_DIR_INDEX
%define KNUM_PAGETABS (1024 - UNUM_PAGETABS)