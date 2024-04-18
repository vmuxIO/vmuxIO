// #include "ice_osdep.h"
#define MAKEMASK(m, s) ((m) << (s))

// from dpdks ice_hw_autogen.h

// available rx/tx queue indexes
#define PFLAN_RX_QALLOC				0x001D2500 /* Reset Source: CORER */
#define PFLAN_RX_QALLOC_FIRSTQ_S		0
#define PFLAN_RX_QALLOC_FIRSTQ_M		MAKEMASK(0x7FF, 0)
#define PFLAN_RX_QALLOC_LASTQ_S			16
#define PFLAN_RX_QALLOC_LASTQ_M			MAKEMASK(0x7FF, 16)
#define PFLAN_RX_QALLOC_VALID_S			31
#define PFLAN_RX_QALLOC_VALID_M			BIT(31)
#define PFLAN_TX_QALLOC				0x001D2580 /* Reset Source: CORER */
#define PFLAN_TX_QALLOC_FIRSTQ_S		0
#define PFLAN_TX_QALLOC_FIRSTQ_M		MAKEMASK(0x3FFF, 0)
#define PFLAN_TX_QALLOC_LASTQ_S			16
#define PFLAN_TX_QALLOC_LASTQ_M			MAKEMASK(0x3FFF, 16)
#define PFLAN_TX_QALLOC_VALID_S			31
#define PFLAN_TX_QALLOC_VALID_M			BIT(31)

// flow director free resources
#define GLQF_FD_SIZE				0x00460010 /* Reset Source: CORER */
#define GLQF_FD_SIZE_FD_GSIZE_S			0
#define GLQF_FD_SIZE_FD_GSIZE_M			MAKEMASK(0x7FFF, 0)
#define GLQF_FD_SIZE_FD_BSIZE_S			16
#define GLQF_FD_SIZE_FD_BSIZE_M			MAKEMASK(0x7FFF, 16)

