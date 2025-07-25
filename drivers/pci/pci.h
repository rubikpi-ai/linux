/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <linux/pci.h>

/* Number of possible devfns: 0.0 to 1f.7 inclusive */
#define MAX_NR_DEVFNS 256

#define MAX_NR_LANES 16

#define PCI_FIND_CAP_TTL	48

#define PCI_VSEC_ID_INTEL_TBT	0x1234	/* Thunderbolt */

#define PCIE_LINK_RETRAIN_TIMEOUT_MS	1000

/* Power stable to PERST# inactive from PCIe card Electromechanical Spec */
#define PCIE_T_PVPERL_MS		100

/*
 * PCIe r6.0, sec 5.3.3.2.1 <PME Synchronization>
 * Recommends 1ms to 10ms timeout to check L2 ready.
 */
#define PCIE_PME_TO_L2_TIMEOUT_US	10000

extern const unsigned char pcie_link_speed[];
extern bool pci_early_dump;

bool pcie_cap_has_lnkctl(const struct pci_dev *dev);
bool pcie_cap_has_lnkctl2(const struct pci_dev *dev);
bool pcie_cap_has_rtctl(const struct pci_dev *dev);

/* Functions internal to the PCI core code */

int pci_create_sysfs_dev_files(struct pci_dev *pdev);
void pci_remove_sysfs_dev_files(struct pci_dev *pdev);
void pci_cleanup_rom(struct pci_dev *dev);
#ifdef CONFIG_DMI
extern const struct attribute_group pci_dev_smbios_attr_group;
#endif

enum pci_mmap_api {
	PCI_MMAP_SYSFS,	/* mmap on /sys/bus/pci/devices/<BDF>/resource<N> */
	PCI_MMAP_PROCFS	/* mmap on /proc/bus/pci/<BDF> */
};
int pci_mmap_fits(struct pci_dev *pdev, int resno, struct vm_area_struct *vmai,
		  enum pci_mmap_api mmap_api);

bool pci_reset_supported(struct pci_dev *dev);
void pci_init_reset_methods(struct pci_dev *dev);
int pci_bridge_secondary_bus_reset(struct pci_dev *dev);
int pci_bus_error_reset(struct pci_dev *dev);
int __pci_reset_bus(struct pci_bus *bus);

struct pci_cap_saved_data {
	u16		cap_nr;
	bool		cap_extended;
	unsigned int	size;
	u32		data[];
};

struct pci_cap_saved_state {
	struct hlist_node		next;
	struct pci_cap_saved_data	cap;
};

void pci_allocate_cap_save_buffers(struct pci_dev *dev);
void pci_free_cap_save_buffers(struct pci_dev *dev);
int pci_add_cap_save_buffer(struct pci_dev *dev, char cap, unsigned int size);
int pci_add_ext_cap_save_buffer(struct pci_dev *dev,
				u16 cap, unsigned int size);
struct pci_cap_saved_state *pci_find_saved_cap(struct pci_dev *dev, char cap);
struct pci_cap_saved_state *pci_find_saved_ext_cap(struct pci_dev *dev,
						   u16 cap);

#define PCI_PM_D2_DELAY         200	/* usec; see PCIe r4.0, sec 5.9.1 */
#define PCI_PM_D3HOT_WAIT       10	/* msec */
#define PCI_PM_D3COLD_WAIT      100	/* msec */

void pci_update_current_state(struct pci_dev *dev, pci_power_t state);
void pci_refresh_power_state(struct pci_dev *dev);
int pci_power_up(struct pci_dev *dev);
void pci_disable_enabled_device(struct pci_dev *dev);
int pci_finish_runtime_suspend(struct pci_dev *dev);
void pcie_clear_device_status(struct pci_dev *dev);
void pcie_clear_root_pme_status(struct pci_dev *dev);
bool pci_check_pme_status(struct pci_dev *dev);
void pci_pme_wakeup_bus(struct pci_bus *bus);
int __pci_pme_wakeup(struct pci_dev *dev, void *ign);
void pci_pme_restore(struct pci_dev *dev);
bool pci_dev_need_resume(struct pci_dev *dev);
void pci_dev_adjust_pme(struct pci_dev *dev);
void pci_dev_complete_resume(struct pci_dev *pci_dev);
void pci_config_pm_runtime_get(struct pci_dev *dev);
void pci_config_pm_runtime_put(struct pci_dev *dev);
void pci_pm_init(struct pci_dev *dev);
void pci_ea_init(struct pci_dev *dev);
void pci_msi_init(struct pci_dev *dev);
void pci_msix_init(struct pci_dev *dev);
bool pci_bridge_d3_possible(struct pci_dev *dev);
void pci_bridge_d3_update(struct pci_dev *dev);
int pci_bridge_wait_for_secondary_bus(struct pci_dev *dev, char *reset_type);

static inline void pci_wakeup_event(struct pci_dev *dev)
{
	/* Wait 100 ms before the system can be put into a sleep state. */
	pm_wakeup_event(&dev->dev, 100);
}

static inline bool pci_has_subordinate(struct pci_dev *pci_dev)
{
	return !!(pci_dev->subordinate);
}

static inline bool pci_power_manageable(struct pci_dev *pci_dev)
{
	/*
	 * Currently we allow normal PCI devices and PCI bridges transition
	 * into D3 if their bridge_d3 is set.
	 */
	return !pci_has_subordinate(pci_dev) || pci_dev->bridge_d3;
}

static inline bool pcie_downstream_port(const struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_DOWNSTREAM ||
	       type == PCI_EXP_TYPE_PCIE_BRIDGE;
}

void pci_vpd_init(struct pci_dev *dev);
void pci_vpd_release(struct pci_dev *dev);
extern const struct attribute_group pci_dev_vpd_attr_group;

/* PCI Virtual Channel */
int pci_save_vc_state(struct pci_dev *dev);
void pci_restore_vc_state(struct pci_dev *dev);
void pci_allocate_vc_save_buffers(struct pci_dev *dev);

/* PCI /proc functions */
#ifdef CONFIG_PROC_FS
int pci_proc_attach_device(struct pci_dev *dev);
int pci_proc_detach_device(struct pci_dev *dev);
int pci_proc_detach_bus(struct pci_bus *bus);
#else
static inline int pci_proc_attach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_bus(struct pci_bus *bus) { return 0; }
#endif

/* Functions for PCI Hotplug drivers to use */
int pci_hp_add_bridge(struct pci_dev *dev);

#ifdef HAVE_PCI_LEGACY
void pci_create_legacy_files(struct pci_bus *bus);
void pci_remove_legacy_files(struct pci_bus *bus);
#else
static inline void pci_create_legacy_files(struct pci_bus *bus) { }
static inline void pci_remove_legacy_files(struct pci_bus *bus) { }
#endif

/* Lock for read/write access to pci device and bus lists */
extern struct rw_semaphore pci_bus_sem;
extern struct mutex pci_slot_mutex;

extern raw_spinlock_t pci_lock;

extern unsigned int pci_pm_d3hot_delay;

#ifdef CONFIG_PCI_MSI
void pci_no_msi(void);
#else
static inline void pci_no_msi(void) { }
#endif

void pci_realloc_get_opt(char *);

static inline int pci_no_d1d2(struct pci_dev *dev)
{
	unsigned int parent_dstates = 0;

	if (dev->bus->self)
		parent_dstates = dev->bus->self->no_d1d2;
	return (dev->no_d1d2 || parent_dstates);

}
extern const struct attribute_group *pci_dev_groups[];
extern const struct attribute_group *pcibus_groups[];
extern const struct device_type pci_dev_type;
extern const struct attribute_group *pci_bus_groups[];

extern unsigned long pci_hotplug_io_size;
extern unsigned long pci_hotplug_mmio_size;
extern unsigned long pci_hotplug_mmio_pref_size;
extern unsigned long pci_hotplug_bus_size;

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *			  PCI device id structure
 * @id: single PCI device id structure to match
 * @dev: the PCI device structure to match against
 *
 * Returns the matching pci_device_id structure or %NULL if there is no match.
 */
static inline const struct pci_device_id *
pci_match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

/* PCI slot sysfs helper code */
#define to_pci_slot(s) container_of(s, struct pci_slot, kobj)

extern struct kset *pci_slots_kset;

struct pci_slot_attribute {
	struct attribute attr;
	ssize_t (*show)(struct pci_slot *, char *);
	ssize_t (*store)(struct pci_slot *, const char *, size_t);
};
#define to_pci_slot_attr(s) container_of(s, struct pci_slot_attribute, attr)

enum pci_bar_type {
	pci_bar_unknown,	/* Standard PCI BAR probe */
	pci_bar_io,		/* An I/O port BAR */
	pci_bar_mem32,		/* A 32-bit memory BAR */
	pci_bar_mem64,		/* A 64-bit memory BAR */
};

struct device *pci_get_host_bridge_device(struct pci_dev *dev);
void pci_put_host_bridge_device(struct device *dev);

int pci_configure_extended_tags(struct pci_dev *dev, void *ign);
bool pci_bus_read_dev_vendor_id(struct pci_bus *bus, int devfn, u32 *pl,
				int crs_timeout);
bool pci_bus_generic_read_dev_vendor_id(struct pci_bus *bus, int devfn, u32 *pl,
					int crs_timeout);
int pci_idt_bus_quirk(struct pci_bus *bus, int devfn, u32 *pl, int crs_timeout);

int pci_setup_device(struct pci_dev *dev);
int __pci_read_base(struct pci_dev *dev, enum pci_bar_type type,
		    struct resource *res, unsigned int reg);
void pci_configure_ari(struct pci_dev *dev);
void __pci_bus_size_bridges(struct pci_bus *bus,
			struct list_head *realloc_head);
void __pci_bus_assign_resources(const struct pci_bus *bus,
				struct list_head *realloc_head,
				struct list_head *fail_head);
bool pci_bus_clip_resource(struct pci_dev *dev, int idx);

void pci_reassigndev_resource_alignment(struct pci_dev *dev);
void pci_disable_bridge_window(struct pci_dev *dev);
struct pci_bus *pci_bus_get(struct pci_bus *bus);
void pci_bus_put(struct pci_bus *bus);

/* PCIe link information from Link Capabilities 2 */
#define PCIE_LNKCAP2_SLS2SPEED(lnkcap2) \
	((lnkcap2) & PCI_EXP_LNKCAP2_SLS_64_0GB ? PCIE_SPEED_64_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_32_0GB ? PCIE_SPEED_32_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_16_0GB ? PCIE_SPEED_16_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_8_0GB ? PCIE_SPEED_8_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_5_0GB ? PCIE_SPEED_5_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_2_5GB ? PCIE_SPEED_2_5GT : \
	 PCI_SPEED_UNKNOWN)

/* PCIe speed to Mb/s reduced by encoding overhead */
#define PCIE_SPEED2MBS_ENC(speed) \
	((speed) == PCIE_SPEED_64_0GT ? 64000*1/1 : \
	 (speed) == PCIE_SPEED_32_0GT ? 32000*128/130 : \
	 (speed) == PCIE_SPEED_16_0GT ? 16000*128/130 : \
	 (speed) == PCIE_SPEED_8_0GT  ?  8000*128/130 : \
	 (speed) == PCIE_SPEED_5_0GT  ?  5000*8/10 : \
	 (speed) == PCIE_SPEED_2_5GT  ?  2500*8/10 : \
	 0)

const char *pci_speed_string(enum pci_bus_speed speed);
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev);
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev);
u32 pcie_bandwidth_capable(struct pci_dev *dev, enum pci_bus_speed *speed,
			   enum pcie_link_width *width);
void __pcie_print_link_status(struct pci_dev *dev, bool verbose);
void pcie_report_downtraining(struct pci_dev *dev);
void pcie_update_link_speed(struct pci_bus *bus, u16 link_status);

/* Single Root I/O Virtualization */
struct pci_sriov {
	int		pos;		/* Capability position */
	int		nres;		/* Number of resources */
	u32		cap;		/* SR-IOV Capabilities */
	u16		ctrl;		/* SR-IOV Control */
	u16		total_VFs;	/* Total VFs associated with the PF */
	u16		initial_VFs;	/* Initial VFs associated with the PF */
	u16		num_VFs;	/* Number of VFs available */
	u16		offset;		/* First VF Routing ID offset */
	u16		stride;		/* Following VF stride */
	u16		vf_device;	/* VF device ID */
	u32		pgsz;		/* Page size for BAR alignment */
	u8		link;		/* Function Dependency Link */
	u8		max_VF_buses;	/* Max buses consumed by VFs */
	u16		driver_max_VFs;	/* Max num VFs driver supports */
	struct pci_dev	*dev;		/* Lowest numbered PF */
	struct pci_dev	*self;		/* This PF */
	u32		class;		/* VF device */
	u8		hdr_type;	/* VF header type */
	u16		subsystem_vendor; /* VF subsystem vendor */
	u16		subsystem_device; /* VF subsystem device */
	resource_size_t	barsz[PCI_SRIOV_NUM_BARS];	/* VF BAR size */
	bool		drivers_autoprobe; /* Auto probing of VFs by driver */
};

#ifdef CONFIG_PCI_DOE
void pci_doe_init(struct pci_dev *pdev);
void pci_doe_destroy(struct pci_dev *pdev);
void pci_doe_disconnected(struct pci_dev *pdev);
#else
static inline void pci_doe_init(struct pci_dev *pdev) { }
static inline void pci_doe_destroy(struct pci_dev *pdev) { }
static inline void pci_doe_disconnected(struct pci_dev *pdev) { }
#endif

/**
 * pci_dev_set_io_state - Set the new error state if possible.
 *
 * @dev: PCI device to set new error_state
 * @new: the state we want dev to be in
 *
 * If the device is experiencing perm_failure, it has to remain in that state.
 * Any other transition is allowed.
 *
 * Returns true if state has been changed to the requested state.
 */
static inline bool pci_dev_set_io_state(struct pci_dev *dev,
					pci_channel_state_t new)
{
	pci_channel_state_t old;

	switch (new) {
	case pci_channel_io_perm_failure:
		xchg(&dev->error_state, pci_channel_io_perm_failure);
		return true;
	case pci_channel_io_frozen:
		old = cmpxchg(&dev->error_state, pci_channel_io_normal,
			      pci_channel_io_frozen);
		return old != pci_channel_io_perm_failure;
	case pci_channel_io_normal:
		old = cmpxchg(&dev->error_state, pci_channel_io_frozen,
			      pci_channel_io_normal);
		return old != pci_channel_io_perm_failure;
	default:
		return false;
	}
}

static inline int pci_dev_set_disconnected(struct pci_dev *dev, void *unused)
{
	pci_dev_set_io_state(dev, pci_channel_io_perm_failure);
	pci_doe_disconnected(dev);

	return 0;
}

/* pci_dev priv_flags */
#define PCI_DEV_ADDED 0
#define PCI_DPC_RECOVERED 1
#define PCI_DPC_RECOVERING 2

static inline void pci_dev_assign_added(struct pci_dev *dev, bool added)
{
	assign_bit(PCI_DEV_ADDED, &dev->priv_flags, added);
}

static inline bool pci_dev_is_added(const struct pci_dev *dev)
{
	return test_bit(PCI_DEV_ADDED, &dev->priv_flags);
}

#ifdef CONFIG_PCIEAER
#include <linux/aer.h>

#define AER_MAX_MULTI_ERR_DEVICES	5	/* Not likely to have more */

struct aer_err_info {
	struct pci_dev *dev[AER_MAX_MULTI_ERR_DEVICES];
	int error_dev_num;

	unsigned int id:16;

	unsigned int severity:2;	/* 0:NONFATAL | 1:FATAL | 2:COR */
	unsigned int __pad1:5;
	unsigned int multi_error_valid:1;

	unsigned int first_error:5;
	unsigned int __pad2:2;
	unsigned int tlp_header_valid:1;

	unsigned int status;		/* COR/UNCOR Error Status */
	unsigned int mask;		/* COR/UNCOR Error Mask */
	struct aer_header_log_regs tlp;	/* TLP Header */
};

int aer_get_device_error_info(struct pci_dev *dev, struct aer_err_info *info);
void aer_print_error(struct pci_dev *dev, struct aer_err_info *info);
#endif	/* CONFIG_PCIEAER */

#ifdef CONFIG_PCIEPORTBUS
/* Cached RCEC Endpoint Association */
struct rcec_ea {
	u8		nextbusn;
	u8		lastbusn;
	u32		bitmap;
};
#endif

#ifdef CONFIG_PCIE_DPC
void pci_save_dpc_state(struct pci_dev *dev);
void pci_restore_dpc_state(struct pci_dev *dev);
void pci_dpc_init(struct pci_dev *pdev);
void dpc_process_error(struct pci_dev *pdev);
pci_ers_result_t dpc_reset_link(struct pci_dev *pdev);
bool pci_dpc_recovered(struct pci_dev *pdev);
#else
static inline void pci_save_dpc_state(struct pci_dev *dev) { }
static inline void pci_restore_dpc_state(struct pci_dev *dev) { }
static inline void pci_dpc_init(struct pci_dev *pdev) { }
static inline bool pci_dpc_recovered(struct pci_dev *pdev) { return false; }
#endif

#ifdef CONFIG_PCIEPORTBUS
void pci_rcec_init(struct pci_dev *dev);
void pci_rcec_exit(struct pci_dev *dev);
void pcie_link_rcec(struct pci_dev *rcec);
void pcie_walk_rcec(struct pci_dev *rcec,
		    int (*cb)(struct pci_dev *, void *),
		    void *userdata);
#else
static inline void pci_rcec_init(struct pci_dev *dev) { }
static inline void pci_rcec_exit(struct pci_dev *dev) { }
static inline void pcie_link_rcec(struct pci_dev *rcec) { }
static inline void pcie_walk_rcec(struct pci_dev *rcec,
				  int (*cb)(struct pci_dev *, void *),
				  void *userdata) { }
#endif

#ifdef CONFIG_PCI_ATS
/* Address Translation Service */
void pci_ats_init(struct pci_dev *dev);
void pci_restore_ats_state(struct pci_dev *dev);
#else
static inline void pci_ats_init(struct pci_dev *d) { }
static inline void pci_restore_ats_state(struct pci_dev *dev) { }
#endif /* CONFIG_PCI_ATS */

#ifdef CONFIG_PCI_PRI
void pci_pri_init(struct pci_dev *dev);
void pci_restore_pri_state(struct pci_dev *pdev);
#else
static inline void pci_pri_init(struct pci_dev *dev) { }
static inline void pci_restore_pri_state(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCI_PASID
void pci_pasid_init(struct pci_dev *dev);
void pci_restore_pasid_state(struct pci_dev *pdev);
#else
static inline void pci_pasid_init(struct pci_dev *dev) { }
static inline void pci_restore_pasid_state(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCI_IOV
int pci_iov_init(struct pci_dev *dev);
void pci_iov_release(struct pci_dev *dev);
void pci_iov_remove(struct pci_dev *dev);
void pci_iov_update_resource(struct pci_dev *dev, int resno);
resource_size_t pci_sriov_resource_alignment(struct pci_dev *dev, int resno);
void pci_restore_iov_state(struct pci_dev *dev);
int pci_iov_bus_range(struct pci_bus *bus);
extern const struct attribute_group sriov_pf_dev_attr_group;
extern const struct attribute_group sriov_vf_dev_attr_group;
#else
static inline int pci_iov_init(struct pci_dev *dev)
{
	return -ENODEV;
}
static inline void pci_iov_release(struct pci_dev *dev) { }
static inline void pci_iov_remove(struct pci_dev *dev) { }
static inline void pci_restore_iov_state(struct pci_dev *dev) { }
static inline int pci_iov_bus_range(struct pci_bus *bus)
{
	return 0;
}

#endif /* CONFIG_PCI_IOV */

#ifdef CONFIG_PCIE_PTM
void pci_ptm_init(struct pci_dev *dev);
void pci_save_ptm_state(struct pci_dev *dev);
void pci_restore_ptm_state(struct pci_dev *dev);
void pci_suspend_ptm(struct pci_dev *dev);
void pci_resume_ptm(struct pci_dev *dev);
#else
static inline void pci_ptm_init(struct pci_dev *dev) { }
static inline void pci_save_ptm_state(struct pci_dev *dev) { }
static inline void pci_restore_ptm_state(struct pci_dev *dev) { }
static inline void pci_suspend_ptm(struct pci_dev *dev) { }
static inline void pci_resume_ptm(struct pci_dev *dev) { }
#endif

unsigned long pci_cardbus_resource_alignment(struct resource *);

static inline resource_size_t pci_resource_alignment(struct pci_dev *dev,
						     struct resource *res)
{
#ifdef CONFIG_PCI_IOV
	int resno = res - dev->resource;

	if (resno >= PCI_IOV_RESOURCES && resno <= PCI_IOV_RESOURCE_END)
		return pci_sriov_resource_alignment(dev, resno);
#endif
	if (dev->class >> 8 == PCI_CLASS_BRIDGE_CARDBUS)
		return pci_cardbus_resource_alignment(res);
	return resource_alignment(res);
}

void pci_acs_init(struct pci_dev *dev);
#ifdef CONFIG_PCI_QUIRKS
int pci_dev_specific_acs_enabled(struct pci_dev *dev, u16 acs_flags);
int pci_dev_specific_enable_acs(struct pci_dev *dev);
int pci_dev_specific_disable_acs_redir(struct pci_dev *dev);
int pcie_failed_link_retrain(struct pci_dev *dev);
#else
static inline int pci_dev_specific_acs_enabled(struct pci_dev *dev,
					       u16 acs_flags)
{
	return -ENOTTY;
}
static inline int pci_dev_specific_enable_acs(struct pci_dev *dev)
{
	return -ENOTTY;
}
static inline int pci_dev_specific_disable_acs_redir(struct pci_dev *dev)
{
	return -ENOTTY;
}
static inline int pcie_failed_link_retrain(struct pci_dev *dev)
{
	return -ENOTTY;
}
#endif

/* PCI error reporting and recovery */
pci_ers_result_t pcie_do_recovery(struct pci_dev *dev,
		pci_channel_state_t state,
		pci_ers_result_t (*reset_subordinates)(struct pci_dev *pdev));

bool pcie_wait_for_link(struct pci_dev *pdev, bool active);
int pcie_retrain_link(struct pci_dev *pdev, bool use_lt);
#ifdef CONFIG_PCIEASPM
void pcie_aspm_init_link_state(struct pci_dev *pdev);
void pcie_aspm_exit_link_state(struct pci_dev *pdev);
void pcie_aspm_pm_state_change(struct pci_dev *pdev, bool locked);
void pcie_aspm_powersave_config_link(struct pci_dev *pdev);
void pci_configure_ltr(struct pci_dev *pdev);
void pci_bridge_reconfigure_ltr(struct pci_dev *pdev);
#else
static inline void pcie_aspm_init_link_state(struct pci_dev *pdev) { }
static inline void pcie_aspm_exit_link_state(struct pci_dev *pdev) { }
static inline void pcie_aspm_pm_state_change(struct pci_dev *pdev, bool locked) { }
static inline void pcie_aspm_powersave_config_link(struct pci_dev *pdev) { }
static inline void pci_configure_ltr(struct pci_dev *pdev) { }
static inline void pci_bridge_reconfigure_ltr(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCIE_ECRC
void pcie_set_ecrc_checking(struct pci_dev *dev);
void pcie_ecrc_get_policy(char *str);
#else
static inline void pcie_set_ecrc_checking(struct pci_dev *dev) { }
static inline void pcie_ecrc_get_policy(char *str) { }
#endif

struct pci_dev_reset_methods {
	u16 vendor;
	u16 device;
	int (*reset)(struct pci_dev *dev, bool probe);
};

struct pci_reset_fn_method {
	int (*reset_fn)(struct pci_dev *pdev, bool probe);
	char *name;
};

#ifdef CONFIG_PCI_QUIRKS
int pci_dev_specific_reset(struct pci_dev *dev, bool probe);
#else
static inline int pci_dev_specific_reset(struct pci_dev *dev, bool probe)
{
	return -ENOTTY;
}
#endif

#if defined(CONFIG_PCI_QUIRKS) && defined(CONFIG_ARM64)
int acpi_get_rc_resources(struct device *dev, const char *hid, u16 segment,
			  struct resource *res);
#else
static inline int acpi_get_rc_resources(struct device *dev, const char *hid,
					u16 segment, struct resource *res)
{
	return -ENODEV;
}
#endif

int pci_rebar_get_current_size(struct pci_dev *pdev, int bar);
int pci_rebar_set_size(struct pci_dev *pdev, int bar, int size);
static inline u64 pci_rebar_size_to_bytes(int size)
{
	return 1ULL << (size + 20);
}

struct device_node;

#define PCI_EQ_RESV	0xff

enum equalization_preset_type {
	EQ_PRESET_TYPE_8GTS,
	EQ_PRESET_TYPE_16GTS,
	EQ_PRESET_TYPE_32GTS,
	EQ_PRESET_TYPE_64GTS,
	EQ_PRESET_TYPE_MAX
};

struct pci_eq_presets {
	u16 eq_presets_8gts[MAX_NR_LANES];
	u8 eq_presets_Ngts[EQ_PRESET_TYPE_MAX - 1][MAX_NR_LANES];
};

#ifdef CONFIG_OF
int of_pci_parse_bus_range(struct device_node *node, struct resource *res);
int of_get_pci_domain_nr(struct device_node *node);
int of_pci_get_max_link_speed(struct device_node *node);
u32 of_pci_get_slot_power_limit(struct device_node *node,
				u8 *slot_power_limit_value,
				u8 *slot_power_limit_scale);
int pci_set_of_node(struct pci_dev *dev);
void pci_release_of_node(struct pci_dev *dev);
void pci_set_bus_of_node(struct pci_bus *bus);
void pci_release_bus_of_node(struct pci_bus *bus);

int of_pci_setup_wake_irq(struct pci_dev *pdev);
void of_pci_teardown_wake_irq(struct pci_dev *pdev);

int devm_of_pci_bridge_init(struct device *dev, struct pci_host_bridge *bridge);

int of_pci_get_equalization_presets(struct device *dev,
				    struct pci_eq_presets *presets,
				    int num_lanes);
#else
static inline int
of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	return -EINVAL;
}

static inline int
of_get_pci_domain_nr(struct device_node *node)
{
	return -1;
}

static inline int
of_pci_get_max_link_speed(struct device_node *node)
{
	return -EINVAL;
}

static inline u32
of_pci_get_slot_power_limit(struct device_node *node,
			    u8 *slot_power_limit_value,
			    u8 *slot_power_limit_scale)
{
	if (slot_power_limit_value)
		*slot_power_limit_value = 0;
	if (slot_power_limit_scale)
		*slot_power_limit_scale = 0;
	return 0;
}

static inline int pci_set_of_node(struct pci_dev *dev) { return 0; }
static inline void pci_release_of_node(struct pci_dev *dev) { }
static inline void pci_set_bus_of_node(struct pci_bus *bus) { }
static inline void pci_release_bus_of_node(struct pci_bus *bus) { }

static inline int devm_of_pci_bridge_init(struct device *dev, struct pci_host_bridge *bridge)
{
	return 0;
}

static int of_pci_setup_wake_irq(struct pci_dev *pdev) { return 0; }
static void of_pci_teardown_wake_irq(struct pci_dev *pdev) { }

static inline int of_pci_get_equalization_presets(struct device *dev,
						  struct pci_eq_presets *presets,
						  int num_lanes)
{
	presets->eq_presets_8gts[0] = PCI_EQ_RESV;
	for (int i = 0; i < EQ_PRESET_TYPE_MAX - 1; i++)
		presets->eq_presets_Ngts[i][0] = PCI_EQ_RESV;

	return 0;
}
#endif /* CONFIG_OF */

struct of_changeset;

#ifdef CONFIG_PCI_DYNAMIC_OF_NODES
void of_pci_make_dev_node(struct pci_dev *pdev);
void of_pci_remove_node(struct pci_dev *pdev);
int of_pci_add_properties(struct pci_dev *pdev, struct of_changeset *ocs,
			  struct device_node *np);
#else
static inline void of_pci_make_dev_node(struct pci_dev *pdev) { }
static inline void of_pci_remove_node(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCIEAER
void pci_no_aer(void);
void pci_aer_init(struct pci_dev *dev);
void pci_aer_exit(struct pci_dev *dev);
extern const struct attribute_group aer_stats_attr_group;
void pci_aer_clear_fatal_status(struct pci_dev *dev);
int pci_aer_clear_status(struct pci_dev *dev);
int pci_aer_raw_clear_status(struct pci_dev *dev);
void pci_save_aer_state(struct pci_dev *dev);
void pci_restore_aer_state(struct pci_dev *dev);
#else
static inline void pci_no_aer(void) { }
static inline void pci_aer_init(struct pci_dev *d) { }
static inline void pci_aer_exit(struct pci_dev *d) { }
static inline void pci_aer_clear_fatal_status(struct pci_dev *dev) { }
static inline int pci_aer_clear_status(struct pci_dev *dev) { return -EINVAL; }
static inline int pci_aer_raw_clear_status(struct pci_dev *dev) { return -EINVAL; }
static inline void pci_save_aer_state(struct pci_dev *dev) { }
static inline void pci_restore_aer_state(struct pci_dev *dev) { }
#endif

#ifdef CONFIG_ACPI
int pci_acpi_program_hp_params(struct pci_dev *dev);
extern const struct attribute_group pci_dev_acpi_attr_group;
void pci_set_acpi_fwnode(struct pci_dev *dev);
int pci_dev_acpi_reset(struct pci_dev *dev, bool probe);
bool acpi_pci_power_manageable(struct pci_dev *dev);
bool acpi_pci_bridge_d3(struct pci_dev *dev);
int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state);
pci_power_t acpi_pci_get_power_state(struct pci_dev *dev);
void acpi_pci_refresh_power_state(struct pci_dev *dev);
int acpi_pci_wakeup(struct pci_dev *dev, bool enable);
bool acpi_pci_need_resume(struct pci_dev *dev);
pci_power_t acpi_pci_choose_state(struct pci_dev *pdev);
#else
static inline int pci_dev_acpi_reset(struct pci_dev *dev, bool probe)
{
	return -ENOTTY;
}
static inline void pci_set_acpi_fwnode(struct pci_dev *dev) { }
static inline int pci_acpi_program_hp_params(struct pci_dev *dev)
{
	return -ENODEV;
}
static inline bool acpi_pci_power_manageable(struct pci_dev *dev)
{
	return false;
}
static inline bool acpi_pci_bridge_d3(struct pci_dev *dev)
{
	return false;
}
static inline int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	return -ENODEV;
}
static inline pci_power_t acpi_pci_get_power_state(struct pci_dev *dev)
{
	return PCI_UNKNOWN;
}
static inline void acpi_pci_refresh_power_state(struct pci_dev *dev) { }
static inline int acpi_pci_wakeup(struct pci_dev *dev, bool enable)
{
	return -ENODEV;
}
static inline bool acpi_pci_need_resume(struct pci_dev *dev)
{
	return false;
}
static inline pci_power_t acpi_pci_choose_state(struct pci_dev *pdev)
{
	return PCI_POWER_ERROR;
}
#endif

#ifdef CONFIG_PCIEASPM
extern const struct attribute_group aspm_ctrl_attr_group;
#endif

extern const struct attribute_group pci_dev_reset_method_attr_group;

#ifdef CONFIG_X86_INTEL_MID
bool pci_use_mid_pm(void);
int mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state);
pci_power_t mid_pci_get_power_state(struct pci_dev *pdev);
#else
static inline bool pci_use_mid_pm(void)
{
	return false;
}
static inline int mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state)
{
	return -ENODEV;
}
static inline pci_power_t mid_pci_get_power_state(struct pci_dev *pdev)
{
	return PCI_UNKNOWN;
}
#endif

/*
 * Config Address for PCI Configuration Mechanism #1
 *
 * See PCI Local Bus Specification, Revision 3.0,
 * Section 3.2.2.3.2, Figure 3-2, p. 50.
 */

#define PCI_CONF1_BUS_SHIFT	16 /* Bus number */
#define PCI_CONF1_DEV_SHIFT	11 /* Device number */
#define PCI_CONF1_FUNC_SHIFT	8  /* Function number */

#define PCI_CONF1_BUS_MASK	0xff
#define PCI_CONF1_DEV_MASK	0x1f
#define PCI_CONF1_FUNC_MASK	0x7
#define PCI_CONF1_REG_MASK	0xfc /* Limit aligned offset to a maximum of 256B */

#define PCI_CONF1_ENABLE	BIT(31)
#define PCI_CONF1_BUS(x)	(((x) & PCI_CONF1_BUS_MASK) << PCI_CONF1_BUS_SHIFT)
#define PCI_CONF1_DEV(x)	(((x) & PCI_CONF1_DEV_MASK) << PCI_CONF1_DEV_SHIFT)
#define PCI_CONF1_FUNC(x)	(((x) & PCI_CONF1_FUNC_MASK) << PCI_CONF1_FUNC_SHIFT)
#define PCI_CONF1_REG(x)	((x) & PCI_CONF1_REG_MASK)

#define PCI_CONF1_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ENABLE | \
	 PCI_CONF1_BUS(bus) | \
	 PCI_CONF1_DEV(dev) | \
	 PCI_CONF1_FUNC(func) | \
	 PCI_CONF1_REG(reg))

/*
 * Extension of PCI Config Address for accessing extended PCIe registers
 *
 * No standardized specification, but used on lot of non-ECAM-compliant ARM SoCs
 * or on AMD Barcelona and new CPUs. Reserved bits [27:24] of PCI Config Address
 * are used for specifying additional 4 high bits of PCI Express register.
 */

#define PCI_CONF1_EXT_REG_SHIFT	16
#define PCI_CONF1_EXT_REG_MASK	0xf00
#define PCI_CONF1_EXT_REG(x)	(((x) & PCI_CONF1_EXT_REG_MASK) << PCI_CONF1_EXT_REG_SHIFT)

#define PCI_CONF1_EXT_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ADDRESS(bus, dev, func, reg) | \
	 PCI_CONF1_EXT_REG(reg))

#endif /* DRIVERS_PCI_H */
