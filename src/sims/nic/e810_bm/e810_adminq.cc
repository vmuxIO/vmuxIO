/*
 * Copyright 2021 Max Planck Institute for Software Systems, and
 * National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sims/nic/e810_bm/e810_ptp.h"
#include <cassert>
#include <iostream>
using namespace std;
#include "sims/nic/e810_bm/e810_base_wrapper.h"
#include "sims/nic/e810_bm/e810_bm.h"
// #include "sims/nic/e810_bm/base/ice_adminq_cmd.h"
#include <bits/stdc++.h>
#include <algorithm>

namespace e810 {

queue_admin_tx::queue_admin_tx(e810_bm &dev_, uint64_t &reg_base_,
                               uint32_t &reg_len_, uint32_t &reg_head_,
                               uint32_t &reg_tail_)
    : queue_base("atx", reg_head_, reg_tail_, dev_),
      reg_base(reg_base_),
      reg_len(reg_len_) {
  desc_len = 32;
  ctxs_init();
}

queue_base::desc_ctx &queue_admin_tx::desc_ctx_create() {
  return *new admin_desc_ctx(*this, dev);
}

void queue_admin_tx::reg_updated() {
  base = reg_base;
  len = (reg_len & PF_FW_ATQLEN_ATQLEN_M);

  if (!enabled && (reg_len & PF_FW_ATQLEN_ATQENABLE_M)) {
#ifdef DEBUG_ADMINQ
    cout << " enable base=" << base << " len=" << len << logger::endl;
#endif
    enabled = true;
  } else if (enabled && !(reg_len & PF_FW_ATQLEN_ATQENABLE_M)) {
#ifdef DEBUG_ADMINQ
    cout << " disable" << logger::endl;
#endif
    enabled = false;
  }

  queue_base::reg_updated();
}

queue_admin_tx::admin_desc_ctx::admin_desc_ctx(queue_admin_tx &queue_,
                                               e810_bm &dev_)
    : e810::queue_base::desc_ctx(queue_), aq(queue_), dev(dev_) {
  d = reinterpret_cast<struct ice_aq_desc *>(desc);
}

void queue_admin_tx::admin_desc_ctx::data_written(uint64_t addr, size_t len) {
  processed();
}

void queue_admin_tx::admin_desc_ctx::desc_compl_prepare(uint16_t retval,
                                                        uint16_t extra_flags) {
  d->flags &= ~0x1ff;
  d->flags |= 0x1 | 0x2 | extra_flags;
  if (retval)
    d->flags |= ICE_AQ_FLAG_ERR;
  d->retval = retval;

#ifdef DEBUG_ADMINQ
  cout <<  " desc_compl_prepare index=" << index << " retval=" << retval
            << logger::endl;
#endif
}

void queue_admin_tx::admin_desc_ctx::desc_complete(uint16_t retval,
                                                   uint16_t extra_flags) {
  desc_compl_prepare(retval, extra_flags);
  processed();
}

void queue_admin_tx::admin_desc_ctx::desc_complete_indir(uint16_t retval,
                                                         const void *data,
                                                         size_t len,
                                                         uint16_t extra_flags,
                                                         bool ignore_datalen) {
  if (!ignore_datalen && len > d->datalen) {
    cout <<  "queue_admin_tx::desc_complete_indir: data too long (" << len
              << ") got buffer for (" << d->datalen << ")" << logger::endl;
    abort();
  }
  d->datalen = len;

  desc_compl_prepare(retval, extra_flags);

  uint64_t addr = d->params.generic.addr_low |
                  (((uint64_t)d->params.generic.addr_high) << 32);
  data_write(addr, len, data);
}

void queue_admin_tx::admin_desc_ctx::prepare() {
  if ((d->flags & ICE_AQ_FLAG_RD)) {
    uint64_t addr = d->params.generic.addr_low |
                    (((uint64_t)d->params.generic.addr_high) << 32);
#ifdef DEBUG_ADMINQ
    cout <<  " desc with buffer opc=" << d->opcode << " addr=" << addr
              << logger::endl;
#endif
    data_fetch(addr, d->datalen);
  } else {
    prepared();
  }
}

void queue_admin_tx::admin_desc_ctx::process() {
#ifdef DEBUG_ADMINQ
  cout <<  " descriptor " << index << " fetched" << logger::endl;
#endif

  if (d->opcode == ice_aqc_opc_get_ver) {
#ifdef DEBUG_ADMINQ
    cout <<  "  get version" << logger::endl;
#endif
    struct ice_aqc_get_ver *gv =
        reinterpret_cast<struct ice_aqc_get_ver *>(d->params.raw);
    gv->rom_ver = 0;
    gv->fw_build = 0;
    gv->fw_major = 6;
    gv->fw_minor = 0;
    gv->api_major = EXP_FW_API_VER_MAJOR;
    gv->api_minor = EXP_FW_API_VER_MINOR;

    desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_req_res) {
#ifdef DEBUG_ADMINQ
    cout <<  "  request resource" << logger::endl;
#endif
    struct ice_aqc_req_res *rr =
        reinterpret_cast<struct ice_aqc_req_res *>(d->params.raw);
    rr->timeout = 180000;
#ifdef DEBUG_ADMINQ
    cout <<  "    res_id=" << rr->res_id << logger::endl;
    cout <<  "    res_nu=" << rr->res_number << logger::endl;
#endif
    desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_release_res) {
#ifdef DEBUG_ADMINQ
    cout <<  "  release resource" << logger::endl;
#endif
#ifdef DEBUG_ADMINQ
    struct ice_aqc_req_res *rr =
        reinterpret_cast<struct ice_aqc_req_res *>(d->params.raw);
    cout <<  "    res_id=" << rr->res_id << logger::endl;
    cout <<  "    res_nu=" << rr->res_number << logger::endl;
#endif
    desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_clear_pxe_mode) {
#ifdef DEBUG_ADMINQ
    cout <<  "  clear PXE mode" << logger::endl;
#endif
    dev.regs.gllan_rctl_0 &= 0;
    desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_list_dev_caps ||
             d->opcode == ice_aqc_opc_list_func_caps) {
#ifdef DEBUG_ADMINQ
    cout <<  "  get dev/fun caps" << logger::endl;
#endif
    struct ice_aqc_list_caps *lc =
        reinterpret_cast<struct ice_aqc_list_caps *>(d->params.raw);
    // if functionatily number larger than 4, rdma is not supported.
    struct ice_aqc_list_caps_elem caps[] = {
        {ICE_AQC_CAPS_VALID_FUNCTIONS, 1, 0, 2, 1, 0, {}, {}},
        {ICE_AQC_CAPS_RSS, 1, 0, 2048, 4, 0, {}, {}},
        {ICE_AQC_CAPS_RXQS, 1, 0, dev.NUM_QUEUES, 0, 0, {}, {}},
        {ICE_AQC_CAPS_TXQS, 1, 0, dev.NUM_QUEUES, 0, 0, {}, {}},
        {ICE_AQC_CAPS_MSIX, 1, 0, dev.NUM_PFINTS, 0, 0, {}, {}},
        {ICE_AQC_CAPS_VSI, 1, 0, dev.NUM_VSIS, 0, 0, {}, {}},
        {ICE_AQC_CAPS_DCB, 1, 0, 1, 4, 1, {}, {}},
        {ICE_AQC_CAPS_IWARP, 1, 0, 1, 1, 1, {}, {}},
        {ICE_AQC_CAPS_FD, 1, 0, dev.NUM_FD_GUAR, dev.NUM_FD_BEST_EFFORT, 0, {}, {}},
        {ICE_AQC_CAPS_1588, 1, 1, CAP_1588_FLAGS, 1, 1, {}, {} }
    };
    size_t num_caps = sizeof(caps) / sizeof(caps[0]);

    if (sizeof(caps) <= d->datalen) {
#ifdef DEBUG_ADMINQ
      cout <<  "    data fits" << logger::endl;
#endif
      // data fits within the buffer
      lc->count = num_caps;
      desc_complete_indir(0, caps, sizeof(caps));
    } else {
#ifdef DEBUG_ADMINQ
      cout <<  "    data doesn't fit" << logger::endl;
#endif
      // data does not fit
      d->datalen = sizeof(caps);
      desc_complete(ICE_AQ_RC_ENOMEM);
    }
  } else if (d->opcode == ice_aqc_opc_lldp_stop) {
#ifdef DEBUG_ADMINQ
    cout <<  "  lldp stop" << logger::endl;
#endif
    desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_manage_mac_read) {
#ifdef DEBUG_ADMINQ
    cout <<  "  read mac" << logger::endl;
#endif
    struct ice_aqc_manage_mac_read *ar =
        reinterpret_cast<struct ice_aqc_manage_mac_read *>(d->params.raw);
    ar->num_addr = 1;
    struct ice_aqc_manage_mac_read_resp ard;
    // uint64_t mac = dev.runner_->GetMacAddr();
    uint64_t mac = dev.vmux->GetMacAddr();
#ifdef DEBUG_ADMINQ
    cout <<  "    mac = " << mac << logger::endl;
#endif
    ard.addr_type = ICE_AQC_MAN_MAC_ADDR_TYPE_LAN;
    memcpy(ard.mac_addr, &mac, 6);

    ar->flags = ICE_AQC_MAN_MAC_LAN_ADDR_VALID | ICE_AQC_MAN_MAC_PORT_ADDR_VALID;
    desc_complete_indir(0, &ard, sizeof(ard));
  } else if (d->opcode == ice_aqc_opc_get_phy_caps) {
#ifdef DEBUG_ADMINQ
    cout <<  "  get phy abilities" << logger::endl;
#endif
    struct ice_aqc_get_phy_caps_data par;
    // memset(&par, 0, sizeof(par));

    par.phy_type_low = ICE_PHY_TYPE_LOW_40GBASE_CR4;
    par.phy_type_high = ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC;
    par.caps = ICE_AQC_PHY_EN_LINK | ICE_AQC_PHY_AN_MODE;
    par.eee_cap = ICE_AQC_PHY_EEE_EN_40GBASE_KR4;

    d->params.generic.param0 = 0;
    d->params.generic.param1 = 0;

    desc_complete_indir(0, &par, sizeof(par));
  } else if (d->opcode == ice_aqc_opc_get_link_status) {
#ifdef DEBUG_ADMINQ
    cout <<  "  link status" << logger::endl;
#endif
    struct ice_aqc_get_link_status *gls =
        reinterpret_cast<struct ice_aqc_get_link_status *>(d->params.raw);

    gls->cmd_flags &= ICE_AQ_LSE_IS_ENABLED;  // should actually return
                                                   // status of link status
                                                   // notification
    struct ice_aqc_get_link_status_data link_status_data;
    // link_status_data.topo_media_conflict = BIT(3);
    link_status_data.phy_type_low = ICE_PHY_TYPE_LOW_40GBASE_CR4;
    // link_status_data.phy_type_high =
    link_status_data.link_speed = ICE_AQ_LINK_SPEED_40GB;
    link_status_data.link_info = ICE_AQ_LINK_UP | ICE_AQ_LINK_UP_PORT |
                     ICE_AQ_MEDIA_AVAILABLE | ICE_AQ_SIGNAL_DETECT;
    // might need qualified module
    link_status_data.an_info = ICE_AQ_AN_COMPLETED | ICE_AQ_LP_AN_ABILITY;
    link_status_data.ext_info = 0;
    // link_status_data.loopback = I40E_AQ_LINK_POWER_CLASS_4 << I40E_AQ_PWR_CLASS_SHIFT_LB;
    link_status_data.max_frame_size = dev.MAX_MTU;
    // link_status_data.config = I40E_AQ_CONFIG_CRC_ENA;

    desc_complete_indir(0, &link_status_data, sizeof(link_status_data));
  } else if (d->opcode == ice_aqc_opc_get_sw_cfg) {
// #ifdef DEBUG_ADMINQ
    cout <<  "  get switch config" << logger::endl;
// #endif
    struct ice_aqc_get_sw_cfg *sw =
        reinterpret_cast<struct ice_aqc_get_sw_cfg *>(d->params.raw);
    struct ice_aqc_get_sw_cfg_resp_elem hr;
    /* Not sure why dpdk doesn't like this?
    struct e810_aqc_switch_config_element_resp els[] = {
        // EMC
        { I40E_AQ_SW_ELEM_TYPE_EMP, I40E_AQ_SW_ELEM_REV_1, 1, 513, 0, {},
            I40E_AQ_CONN_TYPE_REGULAR, 0, 0},
        // MAC
        { I40E_AQ_SW_ELEM_TYPE_MAC, I40E_AQ_SW_ELEM_REV_1, 2, 0, 0, {},
            I40E_AQ_CONN_TYPE_REGULAR, 0, 0},
        // PF
        { I40E_AQ_SW_ELEM_TYPE_PF, I40E_AQ_SW_ELEM_REV_1, 16, 512, 0, {},
            I40E_AQ_CONN_TYPE_REGULAR, 0, 0},
        // VSI PF
        { I40E_AQ_SW_ELEM_TYPE_VSI, I40E_AQ_SW_ELEM_REV_1, 512, 2, 16, {},
            I40E_AQ_CONN_TYPE_REGULAR, 0, 0},
        // VSI PF
        { I40E_AQ_SW_ELEM_TYPE_VSI, I40E_AQ_SW_ELEM_REV_1, 513, 2, 1, {},
            I40E_AQ_CONN_TYPE_REGULAR, 0, 0},
    };*/
    struct ice_aqc_get_sw_cfg_resp_elem els[] = {
        // VSI PF
        {ICE_AQC_GET_SW_CONF_RESP_PHYS_PORT << ICE_AQC_GET_SW_CONF_RESP_TYPE_S | 1, // this used to be ||, but IMHO only bitwise or makes sense here
         0,
         0},
    };

    // find start idx
    size_t cnt = sizeof(els) / sizeof(els[0]);
    size_t first = 0;
    // for (first = 0; first < cnt && els[first].seid < sw->seid; first++) {
    // }

    // figure out how many fit in the buffer
    size_t max = (d->datalen - sizeof(hr)) / sizeof(els[0]);
    size_t report = cnt - first;
    sw->num_elems = 1;

    // prepare header
    // memset(&hr, 0, sizeof(hr));
// #ifdef DEBUG_ADMINQ
//     cout <<  "    report=" << report << " cnt=" << cnt
//               << "  seid=" << sw->seid << logger::endl;
// #endif

    // create temporary contiguous buffer
    size_t buflen = sizeof(hr) + sizeof(els[0]) * report;
    uint8_t buf[buflen];
    memcpy(buf, &hr, sizeof(hr));
    memcpy(buf + sizeof(hr), els + first, sizeof(els[0]) * report);

    desc_complete_indir(0, buf, buflen);
  } else if (d->opcode == ice_aqc_opc_get_pkg_info_list){
    // struct ice_aqc_get_pkg_info_resp *v =
    //     reinterpret_cast<struct ice_aqc_get_pkg_info_resp *>(
    //             d->params.raw);
    struct ice_aqc_get_pkg_info_resp get_pkg_info;
    struct ice_aqc_get_pkg_info pkg_info;
    get_pkg_info.count = 1;
    pkg_info.is_active = 1;
    pkg_info.ver.major = 1;
    pkg_info.ver.minor = 3;
    pkg_info.ver.update = 30;
    pkg_info.ver.draft = 0;
    // char *ice_pkg_name = (char *)malloc(ICE_SEG_NAME_SIZE*sizeof(char));
    // memset(ice_pkg_name, 0, ICE_SEG_NAME_SIZE*sizeof(char));
    char ice_pkg_name_str[ICE_SEG_NAME_SIZE] = "ICE OS Default Package";
    for (int i = 0; i < ICE_SEG_NAME_SIZE; i++)
    {
      pkg_info.name[i] = ice_pkg_name_str[i];
    }
    // ice_pkg_name[i]
    // desc_complete(0);
    size_t buf_len = sizeof(get_pkg_info) + sizeof(pkg_info);
    uint8_t buf[buf_len];
    memcpy(buf, &get_pkg_info, sizeof(get_pkg_info));
    memcpy(buf + sizeof(get_pkg_info), &pkg_info, sizeof(pkg_info));
    desc_complete_indir(0, buf, buf_len);
  } else if (d->opcode == ice_aqc_opc_set_rss_key) {
    struct ice_aqc_get_set_rss_key *v =
        reinterpret_cast<struct ice_aqc_get_set_rss_key *>(
                d->params.raw);

    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_set_rss_lut) {
    struct ice_aqc_get_set_rss_lut *v =
        reinterpret_cast<struct ice_aqc_get_set_rss_lut *>(
                d->params.raw);
    __builtin_dump_struct(v, &printf);
    if (((v->flags & ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_M) >> ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S) == ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512_FLAG) {
      // We use this RSS setting to detect DPDK based Fastclick to fix its unexplainable reg_idx queue offset.
      dev.vsi0_first_queue = 1;
    }
    desc_complete_indir(0, data, d->datalen);
  // }
//   else if (d->opcode == i40e_aqc_opc_set_switch_config) {
// #ifdef DEBUG_ADMINQ
//     cout <<  "  set switch config" << logger::endl;
// #endif
//     /* TODO: lots of interesting things here like l2 filtering etc. that are
//      * relevant.
//     struct i40e_aqc_set_switch_config *sc =
//         reinterpret_cast<struct i40e_aqc_set_switch_config *>(
//                 d->params.raw);
//     */
//     desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_alloc_res) {
    // command to request resources. References a data buffer that contains the actual requests.
    struct ice_aqc_alloc_free_res_cmd *request =
        reinterpret_cast<struct ice_aqc_alloc_free_res_cmd *>(
                d->params.raw);

    // data buffer
    struct ice_aqc_alloc_free_res_elem* resource_request = reinterpret_cast<struct ice_aqc_alloc_free_res_elem*> (data);
    // list of requested things (resource descriptors)
    struct ice_aqc_res_elem* resource_descriptors = (struct ice_aqc_res_elem*) &resource_request->elem;

    printf("ice_aqc_opc_alloc_res type 0x%x buffer address 0x%p\n", resource_request->res_type, resource_descriptors);
    for (int i = 0; i < request->num_entries; i++) {
      __builtin_dump_struct(&resource_descriptors[i], printf);
      uint16_t type = resource_request->res_type & 0x7F; // type is only bits 0:6
      if (type == ICE_AQC_RES_TYPE_VSI_LIST_PRUNE) {
        printf("pruning VSI list %d\n", i);
        resource_descriptors[i].e.sw_resp = i; // these are stubbed resource ids
      } else if (type == ICE_AQC_RES_TYPE_FDIR_COUNTER_BLOCK) {
        printf("allocating FDIR COUNTER BLOCK %d\n", i);
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_FDIR_GUARANTEED_ENTRIES) {
        printf("allocating FDIR guar. entry %d\n", i);
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_FDIR_SHARED_ENTRIES) {
        printf("allocating FDIR shared entry %d\n", i);
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID) {
        printf("allocating ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID\n");
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM) {
        printf("allocating ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM\n");
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID) {
        printf("allocating ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID\n");
        resource_descriptors[i].e.sw_resp = i;
      } else if (type == ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM) {
        printf("allocating ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM\n");
        resource_descriptors[i].e.sw_resp = i;
      } else {
        printf("unknown resource type. Skipping its allocation.\n");
      }
    }

    // dma data to indirect buffer
    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_add_vsi) {
#ifdef DEBUG_ADMINQ
    cout <<  "  get vsi parameters" << logger::endl;
#endif
    struct ice_aqc_add_update_free_vsi_resp *v =
        reinterpret_cast<struct ice_aqc_add_update_free_vsi_resp *>(
                d->params.raw);
    __builtin_dump_struct(v, &printf);
    v->vsi_num = 1;
    struct ice_aqc_vsi_props pd;
    // memset(&pd, 0, sizeof(pd));
    pd.valid_sections |=
        ICE_AQ_VSI_PROP_SW_VALID | ICE_AQ_VSI_PROP_RXQ_MAP_VALID |
        ICE_AQ_VSI_PROP_Q_OPT_VALID;
    desc_complete_indir(0, &pd, sizeof(pd));
  } else if (d->opcode == ice_aqc_opc_update_vsi) {
#ifdef DEBUG_ADMINQ
    cout <<  "  update vsi parameters" << logger::endl;
#endif
    /* TODO */
    desc_complete(0);
//   } else if (d->opcode == i40e_aqc_opc_set_dcb_parameters) {
// #ifdef DEBUG_ADMINQ
//     cout <<  "  set dcb parameters" << logger::endl;
// #endif
//     /* TODO */
//     desc_complete(0);
  } else if (d->opcode == ice_aqc_opc_get_recipe) {
    struct ice_aqc_add_get_recipe *get_recipe_cmd = reinterpret_cast<struct ice_aqc_add_get_recipe*>(d->params.raw);
    struct ice_aqc_recipe_data_elem *get_recipe = reinterpret_cast<struct ice_aqc_recipe_data_elem*>(data);
    cout << "get recipe: reject/ignore" << logger::endl;
    __builtin_dump_struct(get_recipe, &printf);
    desc_complete(0); // TODO this prints, but rejects the command
  } else if (d->opcode == ice_aqc_opc_remove_sw_rules) {
    struct ice_aqc_sw_rules_elem *rules_elem = reinterpret_cast<struct ice_aqc_sw_rules_elem*>(data);
    struct ice_aqc_sw_rules *add_sw_rules_cmd = reinterpret_cast<ice_aqc_sw_rules *> (d->params.raw);

    // assume add_sw_rules_cmd->num_rules_fltr_entry_index == 1
    
    cout <<  "AQ remove sw rule" << logger::endl;

    
    if (rules_elem->type == ICE_AQC_SW_RULES_T_LKUP_RX || rules_elem->type == ICE_AQC_SW_RULES_T_LKUP_TX) {
      rules_elem->pdata.lkup_tx_rx.index = 0; // null, since now deleted
    } else {
      // we should also zero the index for other types
    }

    // TODO peter: actually delete the rule

    int retval = 0; // or ENOSPC on error
    desc_complete_indir(retval, rules_elem, std::min(sizeof(*rules_elem), (size_t)d->datalen));
  } else if (d->opcode == ice_aqc_opc_get_dflt_topo) {
#ifdef DEBUG_ADMINQ
    cout <<  "  configure vsi bw limit" << logger::endl;
#endif
    // cout<<"go to here"<<endl;
    struct ice_aqc_get_topo *get_topo = reinterpret_cast<struct ice_aqc_get_topo *>(
                d->params.raw);
    get_topo->port_num = 1;
    get_topo->num_branches = 2;
    (dev.topo_elem).hdr.num_elems = 7;
    for (int i = 0; i < 7; i++)
    {
      dev.topo_elem.generic[i].node_teid = i;
    }
    dev.topo_elem.generic[0].parent_teid = 0xFFFFFFFF;
    dev.topo_elem.generic[0].data.elem_type = ICE_AQC_ELEM_TYPE_ROOT_PORT;
    
    dev.topo_elem.generic[1].parent_teid = 0;
    dev.topo_elem.generic[1].data.elem_type = ICE_AQC_ELEM_TYPE_ENTRY_POINT; ;

    dev.topo_elem.generic[2].parent_teid = 0;
    dev.topo_elem.generic[2].data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    
    dev.topo_elem.generic[3].parent_teid = 1;
    dev.topo_elem.generic[3].data.elem_type = ICE_AQC_ELEM_TYPE_TC;

    dev.topo_elem.generic[4].parent_teid = 1;
    dev.topo_elem.generic[4].data.elem_type = ICE_AQC_ELEM_TYPE_TC;

    dev.topo_elem.generic[5].parent_teid = 2;
    dev.topo_elem.generic[5].data.elem_type = ICE_AQC_ELEM_TYPE_TC;

    dev.topo_elem.generic[6].parent_teid = 2;
    dev.topo_elem.generic[6].data.elem_type = ICE_AQC_ELEM_TYPE_TC;

    desc_complete_indir(0, &dev.topo_elem, sizeof(dev.topo_elem));
  } else if (d->opcode == ice_aqc_opc_get_sched_elems) {
    struct ice_aqc_sched_elem_cmd *get_elem_cmd = reinterpret_cast<ice_aqc_sched_elem_cmd *> (d->params.raw);
    get_elem_cmd->num_elem_resp = 1;
    struct ice_aqc_txsched_elem_data *get_elem = reinterpret_cast<struct ice_aqc_txsched_elem_data*> (data);

    // Through trial and error we assembled this static tree. Not off of it may be necessary
    switch (get_elem->node_teid)
    {
    case 0:
      get_elem->parent_teid = 0xFFFFFFFF;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_ROOT_PORT;
      break;
    case 1:
      get_elem->parent_teid = 0;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_TC;
      break;
    case 2:
      get_elem->parent_teid = 1;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_ENTRY_POINT;
      break;
    case 3:
      get_elem->parent_teid = 2;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 4:
      get_elem->parent_teid = 3;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 5:
      get_elem->parent_teid = 4;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 6:
      get_elem->parent_teid = 5;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 22:
      get_elem->parent_teid = 5;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 38:
      get_elem->parent_teid = 5;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    case 54: // id 54 
      get_elem->parent_teid = 5;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
      break;
    case 55:
      get_elem->parent_teid = 1;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    case 56:
      get_elem->parent_teid = 55;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    case 57:
      get_elem->parent_teid = 55;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    case 58:
      get_elem->parent_teid = 55;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    case E810_STATIC_NODES: // id 59 // last of the statically allocated nodes
      get_elem->parent_teid = 55;
      get_elem->data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
    default:
      struct ice_aqc_txsched_elem_data* allocated_elem = dev.sched_nodes[get_elem->node_teid];
      if (allocated_elem != NULL) {
        *get_elem = *allocated_elem;
      } else {
        cout << "unexpectedly get elems: "<< get_elem->node_teid << logger::endl; // this is where the driver tries to get more elements and we get confused
      }
      break;
    }
    
    desc_complete_indir(0, get_elem, sizeof(*get_elem));
  } else if (d->opcode == ice_aqc_opc_add_sched_elems) {
    struct ice_aqc_sched_elem_cmd *get_elem_cmd = reinterpret_cast<ice_aqc_sched_elem_cmd *> (d->params.raw);
    struct ice_aqc_add_elem *add_elem = reinterpret_cast<struct ice_aqc_add_elem*> (data);

    get_elem_cmd->num_elem_resp = 1; // get_elem_cmd->num_elem_req; (ignore all but first) // nr. of element groups in data

    __builtin_dump_struct(add_elem, &printf);
    for (int i = 0; i < add_elem->hdr.num_elems; i++) {
      add_elem->generic[i].node_teid = dev.last_returned_node;
      dev.sched_nodes[dev.last_returned_node] = &(add_elem->generic[i]);
      __builtin_dump_struct(&(add_elem->generic[i]), &printf);
      dev.last_returned_node = dev.last_returned_node + 1;
    }

    desc_complete_indir(0, add_elem, d->datalen);
  } else if (d->opcode == ice_aqc_opc_delete_sched_elems) {
    struct ice_aqc_sched_elem_cmd *delete_elem_cmd = reinterpret_cast<ice_aqc_sched_elem_cmd *> (d->params.raw);
    delete_elem_cmd->num_elem_resp = 1;
    struct ice_aqc_delete_elem *delete_elem = reinterpret_cast<struct ice_aqc_delete_elem*> (data);
    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_query_sched_res){
    struct ice_aqc_sched_elem_cmd *query_elem = reinterpret_cast<ice_aqc_sched_elem_cmd *> (d->params.raw);
    query_elem->num_elem_resp = 1;
    struct ice_aqc_query_txsched_res_resp query_res;
    // = reinterpret_cast<struct ice_aqc_query_txsched_res_resp*> (data);
    query_res.sched_props.logical_levels = 4;
    query_res.sched_props.phys_levels = 3;
    query_res.layer_props[0].max_sibl_grp_sz = 1;
    
    query_res.layer_props[1].max_sibl_grp_sz = 2;

    query_res.layer_props[2].max_sibl_grp_sz = 32;
    query_res.layer_props[3].max_sibl_grp_sz = 8;
    
    desc_complete_indir(0, &query_res, sizeof(query_res));
  } else if (d->opcode == ice_aqc_opc_add_txqs) {
    
    struct ice_aqc_add_txqs *add_txqs_cmd = reinterpret_cast<ice_aqc_add_txqs *> (d->params.raw);
    // add_txqs_cmd->num_qgrps = 1;
    struct ice_aqc_add_tx_qgrp *add_txqs = reinterpret_cast<ice_aqc_add_tx_qgrp *> (data);
    uint32_t idx = add_txqs->txqs[0].txq_id;
    __builtin_dump_struct(add_txqs, &printf);
    __builtin_dump_struct(&(add_txqs->txqs[0]), &printf);
    add_txqs->parent_teid = dev.last_used_parent_node;
    add_txqs->num_txqs = 1;
    add_txqs->txqs[0].q_teid = dev.last_returned_node;
    add_txqs->txqs[0].info.elem_type = ICE_AQC_ELEM_TYPE_LEAF;
    add_txqs->txqs[0].info.cir_bw.bw_alloc = 100;
    add_txqs->txqs[0].info.eir_bw.bw_alloc = 100;
    cout<< "ice_aqc_opc_add_txqs. txd id = "<< add_txqs->txqs[0].txq_id << logger::endl;
    if (add_txqs->txqs[0].txq_id >=4){
      cout<< "ice_aqc_opc_add_txqs error. txd id = "<< add_txqs->txqs[0].txq_id << logger::endl;
      // TODO peter: this gets thrown leading to ctx_addr being incorrect, leading to lan_queue_tx::initialize() setting len to 0, leading to devision by 0
      // Problem was that the VM had 8 cores and thus allocated 8 tx queues which is more than the 4 expected here. Likely this number is just arbitrary and can be raised?
    }
    memcpy(dev.ctx_addr[add_txqs->txqs[0].txq_id], add_txqs->txqs[0].txq_ctx, sizeof(u8)*22);
    // if (dev.last_used_parent_node >=3 || dev.last_used_parent_node<=6){
    //   dev.last_used_parent_node = dev.last_used_parent_node + 1;
    // } else {
    //   dev.last_used_parent_node = 3;
    // }

    // dev.last_used_parent_node = dev.last_returned_node+1;
    // dev.lanmgr.qena_updated(add_txqs->txqs[0].txq_id, false);
    dev.regs.qtx_ena[idx] = QRX_CTRL_QENA_REQ_M;
    dev.lanmgr.qena_updated(idx, false);
    desc_complete_indir(0, data, d->datalen);
  // } else if (d->opcode == ice_aqc_opc_add_rdma_qset) {
  //   struct ice_aqc_add_rdma_qset *add_txqs_cmd = reinterpret_cast<ice_aqc_add_rdma_qset *> (d->params.raw);
  //   // add_txqs_cmd->num_qgrps = 1;
  //   struct ice_aqc_add_rdma_qset_data *add_txqs = reinterpret_cast<ice_aqc_add_rdma_qset_data *> (data);
  //   add_txqs->parent_teid = dev.last_used_parent_node;
  //   add_txqs->num_qsets = 1;
  //   add_txqs->rdma_qsets[0].qset_teid = 0;
  //   add_txqs->rdma_qsets[0].tx_qset_id = 0;
    
  //   memcpy(dev.ctx_addr[add_txqs->rdma_qsets[0].qset_teid], add_txqs->rdma_qsets[0].info, sizeof(u8)*22);

  //   dev.last_used_parent_node = dev.last_returned_node+1;
  //   // dev.lanmgr.qena_updated(add_txqs->txqs[0].txq_id, false);
  //   desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_dis_txqs) {
    struct ice_aqc_dis_txqs *dis_txqs_cmd = reinterpret_cast<ice_aqc_dis_txqs *> (d->params.raw);
    struct ice_aqc_dis_txq_item *dis_txqs = reinterpret_cast<ice_aqc_dis_txq_item *> (data);
    dis_txqs->num_qs = 1;
    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_download_pkg) {
    struct ice_aqc_download_pkg *download_pkg = reinterpret_cast<ice_aqc_download_pkg *> (d->params.raw);
    // struct ice_aqc_download_pkg_resp download_pkg_resp = *((struct ice_aqc_download_pkg_resp*)download_pkg);
    struct ice_aqc_download_pkg_resp *download_pkg_resp = (struct ice_aqc_download_pkg_resp*)download_pkg;
    download_pkg_resp->error_info = ICE_AQ_RC_OK;
    download_pkg_resp->error_offset = 0;
    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_add_sw_rules) {
    struct ice_aqc_sw_rules *add_sw_rules_cmd = reinterpret_cast<ice_aqc_sw_rules *> (d->params.raw);
    struct ice_aqc_sw_rules_elem *add_sw_rules = reinterpret_cast<ice_aqc_sw_rules_elem*>(data);
    cout <<  "  set switch config" << logger::endl;
    
    e810_switch::print_sw_rule(add_sw_rules);
    __builtin_dump_struct(add_sw_rules, &printf);

    bool success = dev.bcam.add_rule(add_sw_rules);

    add_sw_rules->type = ICE_AQC_SW_RULES_T_LKUP_TX;
    add_sw_rules->pdata.lkup_tx_rx.src = 1;
    add_sw_rules->pdata.lkup_tx_rx.index = 0;
    desc_complete_indir(0, data, d->datalen);
  } else if (d->opcode == ice_aqc_opc_nvm_read){
    struct ice_aqc_nvm *nvm_read_cmd = reinterpret_cast<ice_aqc_nvm *> (d->params.raw);

    uint32_t offset = 0;
    offset |= nvm_read_cmd->offset_low;
    offset |= (nvm_read_cmd->offset_high) << 16;

    // copy responses observed on real hardware.
    if (offset == 0x0) { // some control register defining nvm layout
      uint16_t return_data = 0x78;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x84) { // more registers used to calculate header length offset
      uint16_t return_data = 0x8020;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x86) {
      uint16_t return_data = 0x41a;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x88) {
      uint16_t return_data = 0x8854;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x8a) {
      uint16_t return_data = 0x7d;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x8c) {
      uint16_t return_data = 0x894e;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x8a) {
      uint16_t return_data = 0x7;
      desc_complete_indir(0, &return_data, sizeof(return_data));

    // two registers that are queried to get a header length:
    // ice_get_nvm_css_hdr_len+58 ICE_NVM_CSS_HDR_LEN_L and *_LEN_H
    } else if (offset == 0x43a004) {
      uint16_t return_data = 0xa1;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    } else if (offset == 0x43a006) {
      uint16_t return_data = 0x0;
      desc_complete_indir(0, &return_data, sizeof(return_data));

    // default value to return for other accesses. This value seems to work.
    } else if (offset < 0xc00000 ) { 
      char return_data = 1 << ICE_SR_CTRL_WORD_1_S ;
      desc_complete_indir(0, &return_data, sizeof(return_data));
    
    // Simulate out of bounds access: Our physical card reports this size at most. Dpdk probes this size.
    } else {
      char return_data = 1 << ICE_SR_CTRL_WORD_1_S ;
      desc_complete_indir(ICE_AQ_RC_EINVAL, &return_data, sizeof(return_data)); // -100 = ICE_ERR_AQ_ERROR
    }

    // desc_complete(0);
  }
  else {
    cout <<  "  unknown opcode=0x" << std::hex << d->opcode << logger::endl;
    
#ifdef DEBUG_ADMINQ
    cout <<  "  unknown opcode=" << d->opcode << logger::endl;
#endif
    // desc_complete(I40E_AQ_RC_ESRCH);
    desc_complete(0);
  }
}
}  // namespace e810
