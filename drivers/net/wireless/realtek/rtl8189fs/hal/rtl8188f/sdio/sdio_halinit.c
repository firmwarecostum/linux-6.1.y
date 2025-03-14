/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _SDIO_HALINIT_C_

#include <rtl8188f_hal.h>
#include "hal_com_h2c.h"

/*
 * Description:
 *	Call power on sequence to enable card
 *
 * Return:
 *	_SUCCESS	enable success
 *	_FAIL		enable fail
 */
static u8 CardEnable(PADAPTER padapter)
{
	u8 bMacPwrCtrlOn;
	u8 ret = _FAIL;

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE) {
		u8 hci_sus_state;

		// RSV_CTRL 0x1C[7:0] = 0x00
		// unlock ISO/CLK/Power control register
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		hci_sus_state = HCI_SUS_LEAVING;
		rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8188F_card_enable_flow);
		if (ret == _SUCCESS) {
			bMacPwrCtrlOn = _TRUE;
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
			hci_sus_state = HCI_SUS_LEAVE;
			rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
		} else {
			hci_sus_state = HCI_SUS_ERR;
			rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
		}
	} else
		ret = _SUCCESS;

	return ret;
}

/*
 * Description:
 *	Call this function to make sure power on successfully
 *
 * Return:
 *	_SUCCESS	enable success
 *	_FAIL	enable fail
 */
static int PowerOnCheck(PADAPTER padapter)
{
	u32	val_offset0, val_offset1, val_offset2, val_offset3;
	u32 val_mix = 0;
	u32 res = 0;
	u8	ret = _FAIL;
	int index = 0;

	val_offset0 = rtw_read8(padapter, REG_CR);
	val_offset1 = rtw_read8(padapter, REG_CR+1);
	val_offset2 = rtw_read8(padapter, REG_CR+2);
	val_offset3 = rtw_read8(padapter, REG_CR+3);

	if (val_offset0 == 0xEA || val_offset1 == 0xEA ||
			val_offset2 == 0xEA || val_offset3 ==0xEA) {
		DBG_871X("%s: power on fail, do Power on again\n", __func__);
		return ret;
	}

	val_mix = val_offset3 << 24 | val_mix;
	val_mix = val_offset2 << 16 | val_mix;
	val_mix = val_offset1 << 8 | val_mix;
	val_mix = val_offset0 | val_mix;

	res = rtw_read32(padapter, REG_CR);

	DBG_871X("%s: val_mix:0x%08x, res:0x%08x\n", __func__, val_mix, res);

	while(index < 100) {
		if (res == val_mix) {
			DBG_871X("%s: 0x100 the result of cmd52 and cmd53 is the same.\n", __func__);
			ret = _SUCCESS;
			break;
		} else {
			DBG_871X("%s: 0x100 cmd52 and cmd53 is not the same(index:%d).\n", __func__, index);
			res = rtw_read32(padapter, REG_CR);
			index ++;
			ret = _FAIL;
		}
	}

	if (ret) {
		index = 0;
		while(index < 100) {
			rtw_write32(padapter, 0x1B8, 0x12345678);
			res = rtw_read32(padapter, 0x1B8);
			if (res == 0x12345678) {
				DBG_871X("%s: 0x1B8 test Pass.\n", __func__);
				ret = _SUCCESS;
				break;
			} else {
				index ++;
				DBG_871X("%s: 0x1B8 test Fail(index: %d).\n", __func__, index);
				ret = _FAIL;
			}
		}
	} else {
		DBG_871X("%s: fail at cmd52, cmd53.\n", __func__);
	}

	if (ret == _FAIL) {
		DBG_871X_LEVEL(_drv_err_, "Dump MAC Page0 register:\n");
		/* Dump Page0 for check cystal*/
		for (index = 0 ; index < 0xff ; index++) {
			if(index%16==0)
				printk("0x%02x ",index);

			printk("%02x ", rtw_read8(padapter, index)); 

			if(index%16==15)
				printk("\n");
			else if(index%8==7)
				printk("\t");
		}
		printk("\n");
	}

	return ret;
}

static u32 _InitPowerOn_8188FS(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;
	u8 pwron_chk_cnt=0;
//	u8 bMacPwrCtrlOn;

_init_power_on:

	/* check to apply user defined pll_ref_clk_sel */
	if ((regsty->pll_ref_clk_sel & 0x0F) != 0x0F)
		rtl8188f_set_pll_ref_clk_sel(padapter, regsty->pll_ref_clk_sel);

#ifdef CONFIG_EXT_CLK
	// Use external crystal(XTAL)
	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8188F+2);
	value8 |=  BIT(7);
	rtw_write8(padapter, REG_PAD_CTRL1_8188F+2, value8);

	// CLK_REQ High active or Low Active
	// Request GPIO polarity:
	// 0: low active
	// 1: high active
	value8 = rtw_read8(padapter, REG_MULTI_FUNC_CTRL+1);
	value8 |= BIT(5);
	rtw_write8(padapter, REG_MULTI_FUNC_CTRL+1, value8);
#endif // CONFIG_EXT_CLK

	// only cmd52 can be used before power on(card enable)
	ret = CardEnable(padapter);
	if (ret == _FALSE) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_emerg_,
				("%s: run power on flow fail\n", __FUNCTION__));
		return _FAIL;
	}

	rtw_write8(padapter, REG_CR, 0x00);
	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	rtw_write16(padapter, REG_CR, value16);


	//PowerOnCheck()
	ret = PowerOnCheck(padapter);
	pwron_chk_cnt++;	
	if (_FAIL == ret ) {	
		if (pwron_chk_cnt > 1) {
			DBG_871X("Failed to init Power On!\n");
			return _FAIL;
		}
		DBG_871X("Power on Fail! do it again\n");
		goto _init_power_on;
	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_PowerOnSetting(padapter);

	// external switch to S1
	// 0x38[11] = 0x1
	// 0x4c[23] = 0x1
	// 0x64[0] = 0
	value16 = rtw_read16(padapter, REG_PWR_DATA);
	// Switch the control of EESK, EECS to RFC for DPDT or Antenna switch
	value16 |= BIT(11); // BIT_EEPRPAD_RFE_CTRL_EN
	rtw_write16(padapter, REG_PWR_DATA, value16);
//	DBG_8192C("%s: REG_PWR_DATA(0x%x)=0x%04X\n", __FUNCTION__, REG_PWR_DATA, rtw_read16(padapter, REG_PWR_DATA));

	value32 = rtw_read32(padapter, REG_LEDCFG0);
	value32 |= BIT(23); // DPDT_SEL_EN, 1 for SW control
	rtw_write32(padapter, REG_LEDCFG0, value32);
//	DBG_8192C("%s: REG_LEDCFG0(0x%x)=0x%08X\n", __FUNCTION__, REG_LEDCFG0, rtw_read32(padapter, REG_LEDCFG0));

	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8188F);
	value8 &= ~BIT(0); // BIT_SW_DPDT_SEL_DATA, DPDT_SEL default configuration
	rtw_write8(padapter, REG_PAD_CTRL1_8188F, value8);
//	DBG_8192C("%s: REG_PAD_CTRL1(0x%x)=0x%02X\n", __FUNCTION__, REG_PAD_CTRL1_8188F, rtw_read8(padapter, REG_PAD_CTRL1_8188F));
#endif // CONFIG_BT_COEXIST

	return _SUCCESS;
}

//Tx Page FIFO threshold
static void _init_available_page_threshold(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ)
{
	u16	HQ_threshold, NQ_threshold, LQ_threshold;

	HQ_threshold = (numPubQ + numHQ + 1) >> 1;
	HQ_threshold |= (HQ_threshold<<8);

	NQ_threshold = (numPubQ + numNQ + 1) >> 1;
	NQ_threshold |= (NQ_threshold<<8);

	LQ_threshold = (numPubQ + numLQ + 1) >> 1;
	LQ_threshold |= (LQ_threshold<<8);

	rtw_write16(padapter, 0x218, HQ_threshold);
	rtw_write16(padapter, 0x21A, NQ_threshold);
	rtw_write16(padapter, 0x21C, LQ_threshold);
	DBG_8192C("%s(): Enable Tx FIFO Page Threshold H:0x%x,N:0x%x,L:0x%x\n", __FUNCTION__, HQ_threshold, NQ_threshold, LQ_threshold);
}

static void _InitQueueReservedPage(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	BOOLEAN			bWiFiConfig	= pregistrypriv->wifi_spec;

	if (pHalData->OutEpQueueSel & TX_SELE_HQ)
	{
		numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ_8188F : NORMAL_PAGE_NUM_HPQ_8188F;
	}

	if (pHalData->OutEpQueueSel & TX_SELE_LQ)
	{
		numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ_8188F : NORMAL_PAGE_NUM_LPQ_8188F;
	}

	// NOTE: This step shall be proceed before writting REG_RQPN.
	if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
		numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ_8188F : NORMAL_PAGE_NUM_NPQ_8188F;
	}

	numPubQ = TX_TOTAL_PAGE_NUMBER_8188F - numHQ - numLQ - numNQ;

	value8 = (u8)_NPQ(numNQ);
	rtw_write8(padapter, REG_RQPN_NPQ, value8);

	// TX DMA
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);

	rtw_hal_set_sdio_tx_max_length(padapter, numHQ, numNQ, numLQ, numPubQ);

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
	_init_available_page_threshold(padapter, numHQ, numNQ, numLQ, numPubQ);
#endif
}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_CONCURRENT_MODE
	u8 val8;
#endif // CONFIG_CONCURRENT_MODE

	//u16	txdmactrl;
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_8188F;
	} else {
		//for WMM
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8188F;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);

#ifdef CONFIG_CONCURRENT_MODE
	val8 = txpktbuf_bndy + BCNQ_PAGE_NUM_8188F + WOWLAN_PAGE_NUM_8188F;
	rtw_write8(padapter, REG_BCNQ1_BDNY, val8);
	rtw_write8(padapter, REG_DWBCN1_CTRL_8188F+1, val8); // BCN1_HEAD

	val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8188F+2);
	val8 |= BIT(1); // BIT1- BIT_SW_BCN_SEL_EN
	rtw_write8(padapter, REG_DWBCN1_CTRL_8188F+2, val8);
#endif // CONFIG_CONCURRENT_MODE
}

static VOID
_InitNormalChipRegPriority(
	IN	PADAPTER	Adapter,
	IN	u16		beQ,
	IN	u16		bkQ,
	IN	u16		viQ,
	IN	u16		voQ,
	IN	u16		mgtQ,
	IN	u16		hiQ
	)
{
	u16 value16		= (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |=	_TXDMA_BEQ_MAP(beQ) 	| _TXDMA_BKQ_MAP(bkQ) |
				_TXDMA_VIQ_MAP(viQ) 	| _TXDMA_VOQ_MAP(voQ) |
				_TXDMA_MGQ_MAP(mgtQ)| _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static VOID
_InitNormalChipOneOutEpPriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	u16	value = 0;
	switch(pHalData->OutEpQueueSel)
	{
		case TX_SELE_HQ:
			value = QUEUE_HIGH;
			break;
		case TX_SELE_LQ:
			value = QUEUE_LOW;
			break;
		case TX_SELE_NQ:
			value = QUEUE_NORMAL;
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}

	_InitNormalChipRegPriority(Adapter,
								value,
								value,
								value,
								value,
								value,
								value
								);

}

static VOID
_InitNormalChipTwoOutEpPriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ,bkQ,viQ,voQ,mgtQ,hiQ;


	u16	valueHi = 0;
	u16	valueLow = 0;

	switch(pHalData->OutEpQueueSel)
	{
		case (TX_SELE_HQ | TX_SELE_LQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_NQ | TX_SELE_LQ):
			valueHi = QUEUE_NORMAL;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_HQ | TX_SELE_NQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_NORMAL;
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}

	if(!pregistrypriv->wifi_spec ){
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}
	else{//for WMM ,CONFIG_OUT_EP_WIFI_MODE
		beQ		= valueLow;
		bkQ		= valueHi;
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}

	_InitNormalChipRegPriority(Adapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);

}

static VOID
_InitNormalChipThreeOutEpPriority(
	IN	PADAPTER padapter
	)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec){// typical setting
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_LOW;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;
	}
	else {// for WMM
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_NORMAL;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(padapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);
}

static VOID
_InitNormalChipQueuePriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	switch(pHalData->OutEpNumber)
	{
		case 1:
			_InitNormalChipOneOutEpPriority(Adapter);
			break;
		case 2:
			_InitNormalChipTwoOutEpPriority(Adapter);
			break;
		case 3:
			_InitNormalChipThreeOutEpPriority(Adapter);
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}


}

static void _InitQueuePriority(PADAPTER padapter)
{
	_InitNormalChipQueuePriority(padapter);
}

static void _InitPageBoundary(PADAPTER padapter)
{
	// RX Page Boundary
	u16 rxff_bndy = RX_DMA_BOUNDARY_8188F;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
}

static void _InitTransferPageSize(PADAPTER padapter)
{
	// Tx page size is always 128.

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}

void _InitDriverInfoSize(PADAPTER padapter, u8 drvInfoSize)
{
	rtw_write8(padapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

void _InitNetworkType(PADAPTER padapter)
{
	u32 value32;

	value32 = rtw_read32(padapter, REG_CR);

	// TODO: use the other function to set network type
//	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(padapter, REG_CR, value32);
}

void _InitWMACSetting(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u16 value16;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->ReceiveConfig = 0;
	pHalData->ReceiveConfig |= RCR_APM | RCR_AM | RCR_AB;
	pHalData->ReceiveConfig |= RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_AMF;
	pHalData->ReceiveConfig |= RCR_HTC_LOC_CTRL;
	pHalData->ReceiveConfig |= RCR_APP_PHYST_RXFF | RCR_APP_ICV | RCR_APP_MIC;
#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	pHalData->ReceiveConfig |= RCR_AAP;
	pHalData->ReceiveConfig |= RCR_ADD3 | RCR_APWRMGT | RCR_ACRC32 | RCR_ADF;
#endif
	rtw_write32(padapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all multicast address
	rtw_write32(padapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(padapter, REG_MAR + 4, 0xFFFFFFFF);

	// Accept all data frames
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP2, value16);

	// 2010.09.08 hpfan
	// Since ADF is removed from RCR, ps-poll will not be indicate to driver,
	// RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll.
	value16 = 0x400;
	rtw_write16(padapter, REG_RXFLTMAP1, value16);

	// Accept all management frames
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP0, value16);
}

void _InitAdaptiveCtrl(PADAPTER padapter)
{
	u16	value16;
	u32	value32;

	// Response Rate Set
	value32 = rtw_read32(padapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(padapter, REG_RRSR, value32);

	// CF-END Threshold
	//m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1);

	// SIFS (used in NAV)
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(padapter, REG_SPEC_SIFS, value16);

	// Retry Limit
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(padapter, REG_RL, value16);
}

void _InitEDCA(PADAPTER padapter)
{
	// Set Spec SIFS (used in NAV)
	rtw_write16(padapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(padapter, REG_MAC_SPEC_SIFS, 0x100a);

	// Set SIFS for CCK
	rtw_write16(padapter, REG_SIFS_CTX, 0x100a);

	// Set SIFS for OFDM
	rtw_write16(padapter, REG_SIFS_TRX, 0x100a);

	// TXOP
	rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002FA226);

	rtw_write8(padapter, REG_USTIME_EDCA_8188F, 0x28);
	rtw_write8(padapter, REG_USTIME_TSF_8188F, 0x28);
}

void _InitRateFallback(PADAPTER padapter)
{
	// Set Data Auto Rate Fallback Retry Count register.
	rtw_write32(padapter, REG_DARFRC, 0x00000000);
	rtw_write32(padapter, REG_DARFRC+4, 0x10080404);
	rtw_write32(padapter, REG_RARFRC, 0x04030201);
	rtw_write32(padapter, REG_RARFRC+4, 0x08070605);

}

void _InitRetryFunction(PADAPTER padapter)
{
	u8	value8;

	value8 = rtw_read8(padapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL, value8);

	// Set ACK timeout
	rtw_write8(padapter, REG_ACKTO, 0x40);
}

static void HalRxAggr8188FSdio(PADAPTER padapter)
{
	u8	dma_time_th;
	u8	dma_len_th;

	dma_time_th = 0x01;
	dma_len_th = 0x0f;

	if (dma_len_th * 1024 > MAX_RECVBUF_SZ) {
		DBG_871X_LEVEL(_drv_always_, "Reduce RXDMA_AGG_LEN_TH from %u to %u\n"
			, dma_len_th, MAX_RECVBUF_SZ / 1024);
		dma_len_th = MAX_RECVBUF_SZ / 1024;
	}

	rtw_write16(padapter, REG_RXDMA_AGG_PG_TH, (dma_time_th << 8) | dma_len_th);
}

void sdio_AggSettingRxUpdate(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData;
	u8 valueDMA;
	u8 valueRxAggCtrl = 0;
	u8 aggBurstNum = 3;  //0:1, 1:2, 2:3, 3:4
	u8 aggBurstSize = 0;  //0:1K, 1:512Byte, 2:256Byte...

	pHalData = GET_HAL_DATA(padapter);

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueDMA |= RXDMA_AGG_EN;
	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);

	valueRxAggCtrl |= RXDMA_AGG_MODE_EN;
	valueRxAggCtrl |= ((aggBurstNum<<2) & 0x0C);
	valueRxAggCtrl |= ((aggBurstSize<<4) & 0x30);  
	rtw_write8(padapter, REG_RXDMA_MODE_CTRL_8188F, valueRxAggCtrl);//RxAggLowThresh = 4*1K
}

void _initSdioAggregationSetting(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	// Tx aggregation setting
//	sdio_AggSettingTxUpdate(padapter);

	// Rx aggregation setting
	HalRxAggr8188FSdio(padapter);

	sdio_AggSettingRxUpdate(padapter);
}

static void _RXAggrSwitch(PADAPTER padapter, u8 enable)
{
	PHAL_DATA_TYPE pHalData;
	u8 valueDMA;
	u8 valueRxAggCtrl;


	pHalData = GET_HAL_DATA(padapter);

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueRxAggCtrl = rtw_read8(padapter, REG_RXDMA_MODE_CTRL_8188F);

	if (_TRUE == enable)
	{
		valueDMA |= RXDMA_AGG_EN;
		valueRxAggCtrl |= RXDMA_AGG_MODE_EN;
	}
	else
	{
		valueDMA &= ~RXDMA_AGG_EN;
		valueRxAggCtrl &= ~RXDMA_AGG_MODE_EN;
	}

	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(padapter, REG_RXDMA_MODE_CTRL_8188F, valueRxAggCtrl);
}

void _InitOperationMode(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct mlme_ext_priv *pmlmeext;
	u8				regBwOpMode = 0;
	u32				regRATR = 0, regRRSR = 0;
	u8				MinSpaceCfg = 0;


	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;

	//1 This part need to modified according to the rate set we filtered!!
	//
	// Set RRSR, RATR, and REG_BWOPMODE registers
	//
	switch(pmlmeext->cur_wireless_mode)
	{
		case WIRELESS_MODE_B:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK;
			regRRSR = RATE_ALL_CCK;
			break;
		case WIRELESS_MODE_A:
//			RT_ASSERT(FALSE,("Error wireless a mode\n"));
#if 0
			regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;
		case WIRELESS_MODE_G:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			break;
		case WIRELESS_MODE_AUTO:
#if 0
			if (padapter->bInHctTest)
			{
				regBwOpMode = BW_OPMODE_20MHZ;
				regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
				regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			}
			else
#endif
			{
				regBwOpMode = BW_OPMODE_20MHZ;
				regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
				regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			}
			break;
		case WIRELESS_MODE_N_24G:
			// It support CCK rate by default.
			// CCK rate will be filtered out only when associated AP does not support it.
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			break;
		case WIRELESS_MODE_N_5G:
//			RT_ASSERT(FALSE,("Error wireless mode"));
#if 0
			regBwOpMode = BW_OPMODE_5G;
			regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;

		default: //for MacOSX compiler warning.
			break;
	}

	rtw_write8(padapter, REG_BWOPMODE, regBwOpMode);

}

void _InitInterrupt(PADAPTER padapter)
{
	//
	// Initialize and enable SDIO Host Interrupt.
	//
	InitInterrupt8188FSdio(padapter);

	//
	// Initialize system Host Interrupt.
	//
	InitSysInterrupt8188FSdio(padapter);
}

void _InitRDGSetting(PADAPTER padapter)
{
	rtw_write8(padapter, REG_RD_CTRL, 0xFF);
	rtw_write16(padapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(padapter, REG_RD_RESP_PKT_TH, 0x05);
}

static void _InitRFType(PADAPTER padapter)
{
	struct registry_priv *pregpriv = &padapter->registrypriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif
	pHalData->rf_chip	= RF_6052;

	MSG_8192C("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);
}

static void _RfPowerSave(PADAPTER padapter)
{
//YJ,TODO
}

static void _InitAntenna_Selection(PADAPTER padapter)
{
	rtw_write8(padapter, REG_LEDCFG2, 0x82);
}

static void _InitPABias(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	u8			pa_setting;

	//FIXED PA current issue
	//efuse_one_byte_read(padapter, 0x1FA, &pa_setting);
	pa_setting = EFUSE_Read1Byte(padapter, 0x1FA);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("_InitPABias 0x1FA 0x%x \n",pa_setting));

	if(!(pa_setting & BIT0))
	{
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x4F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x8F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0xCF406);
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path A\n"));
	}

	if(!(pa_setting & BIT4))
	{
		pa_setting = rtw_read8(padapter, 0x16);
		pa_setting &= 0x0F;
		rtw_write8(padapter, 0x16, pa_setting | 0x80);
		rtw_write8(padapter, 0x16, pa_setting | 0x90);
	}
}

//
// 2010/08/09 MH Add for power down check.
//
static BOOLEAN HalDetectPwrDownMode(PADAPTER Adapter)
{
	u8 tmpvalue;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);


	EFUSE_ShadowRead(Adapter, 1, 0x7B/*EEPROM_RF_OPT3_92C*/, (u32 *)&tmpvalue);

	// 2010/08/25 MH INF priority > PDN Efuse value.
	if(tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
	{
		pHalData->pwrdown = _TRUE;
	}
	else
	{
		pHalData->pwrdown = _FALSE;
	}

	DBG_8192C("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	// HalDetectPwrDownMode

static u32 rtl8188fs_hal_init(PADAPTER padapter)
{
	s32 ret;
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct registry_priv *pregistrypriv;
	struct sreset_priv *psrtpriv;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	rt_rf_power_state eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;
	u8 u1bTmp;
	u16 value16;
	u8 typeid;
	u32 u4Tmp;

	pHalData = GET_HAL_DATA(padapter);
	psrtpriv = &pHalData->srestpriv;
	pwrctrlpriv = adapter_to_pwrctl(padapter);
	pregistrypriv = &padapter->registrypriv;

#ifdef CONFIG_SWLPS_IN_IPS
	if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE)
	{
		u8 val8, bMacPwrCtrlOn = _TRUE;

		DBG_871X("%s: run LPS flow in IPS\n", __FUNCTION__);

		//ser rpwm
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;	
		val8 |= BIT(6);		
		rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, val8);
		
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
		
		rtw_mdelay_os(5); //wait set rpwm already
		
		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8188F_leave_swlps_flow);
		if (ret == _FALSE) {
			DBG_8192C("%s: run LPS flow in IPS fail!\n", __FUNCTION__);
			return _FAIL;
		}

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

		pHalData->LastHMEBoxNum = 0;


		return _SUCCESS;
	}
#elif defined(CONFIG_FWLPS_IN_IPS)
	if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE
		&& adapter_to_pwrctl(padapter)->pre_ips_type == 0)
	{
		u32 start_time;
		u8 cpwm_orig, cpwm_now;
		u8 val8, bMacPwrCtrlOn = _TRUE;

		DBG_871X("%s: Leaving IPS in FWLPS state\n", __FUNCTION__);

		//for polling cpwm
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		//ser rpwm
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;	
		val8 |= BIT(6);		
		rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, val8);
		DBG_871X("%s: write rpwm=%02x\n", __FUNCTION__, val8);
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		//do polling cpwm
		start_time = rtw_get_current_time();		
		do {

			rtw_mdelay_os(1);
			
			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80)
			{		
#ifdef DBG_CHECK_FW_PS_STATE				
				DBG_871X("%s: polling cpwm ok when leaving IPS in FWLPS state, cpwm_orig=%02x, cpwm_now=%02x, 0x100=0x%x \n"
				, __FUNCTION__, cpwm_orig, cpwm_now, rtw_read8(padapter, REG_CR));
#endif //DBG_CHECK_FW_PS_STATE
				break;
			}

			if (rtw_get_passing_time_ms(start_time) > 100)
			{
				DBG_871X("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __FUNCTION__);
				break;
			}			
		} while (1);

		rtl8188f_set_FwPwrModeInIPS_cmd(padapter, 0);

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);		


#ifdef DBG_CHECK_FW_PS_STATE
		if(rtw_fw_ps_state(padapter) == _FAIL)
		{
			DBG_871X("after hal init, fw ps state in 32k\n");
			pdbgpriv->dbg_ips_drvopen_fail_cnt++;
		}
#endif //DBG_CHECK_FW_PS_STATE
		return _SUCCESS;
	}	
#endif //CONFIG_SWLPS_IN_IPS

	// Disable Interrupt first.
//	rtw_hal_disable_interrupt(padapter);

	if(rtw_read8(padapter, REG_MCUFWDL) == 0xc6) {
		DBG_871X("FW exist before power on!!\n");
	} else {
		DBG_871X("FW does not exist before power on!!\n");
	}

	if(rtw_fw_ps_state(padapter) == _FAIL)
	{
		DBG_871X("check fw_ps_state fail before PowerOn!\n");
		pdbgpriv->dbg_ips_drvopen_fail_cnt++;
	}
	
	ret = rtw_hal_power_on(padapter);
	if (_FAIL == ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init Power On!\n"));
		return _FAIL;
	}
	DBG_871X("Power on ok!\n");
	
	if(rtw_fw_ps_state(padapter) == _FAIL)
	{
		DBG_871X("check fw_ps_state fail after PowerOn!\n");
		pdbgpriv->dbg_ips_drvopen_fail_cnt++;
	}	
	

	rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);

	if (padapter->registrypriv.mp_mode == 0
		#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
		|| padapter->registrypriv.mp_customer_str
		#endif
	) {
		ret = rtl8188f_FirmwareDownload(padapter, _FALSE);
		if (ret != _SUCCESS) {
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("%s: Download Firmware failed!!\n", __FUNCTION__));
			padapter->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			return ret;
		} else {
			RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("rtl8188fs_hal_init(): Download Firmware Success!!\n"));
			padapter->bFWReady = _TRUE;
			pHalData->fw_ractrl = _TRUE;
		}
	}

//	SIC_Init(padapter);

	if (pwrctrlpriv->reg_rfoff == _TRUE) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	HalDetectPwrDownMode(padapter);

	// Set RF type for BB/RF configuration
	_InitRFType(padapter);

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	pHalData->CurrentChannel = 6;

#if (HAL_MAC_ENABLE == 1)
	ret = PHY_MACConfig8188F(padapter);
	if(ret != _SUCCESS){
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure MAC!!\n"));
		return ret;
	}
#endif
	//
	//d. Initialize BB related configurations.
	//
#if (HAL_BB_ENABLE == 1)
	ret = PHY_BBConfig8188F(padapter);
	if(ret != _SUCCESS){
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure BB!!\n"));
		return ret;
	}
#endif

	// If RF is on, we need to init RF. Otherwise, skip the procedure.
	// We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode.
	//if(pHalData->eRFPowerState == eRfOn)
	{
#if (HAL_RF_ENABLE == 1)
		ret = PHY_RFConfig8188F(padapter);
		if(ret != _SUCCESS){
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure RF!!\n"));
			return ret;
		}
#endif
	}

	//
	// Joseph Note: Keep RfRegChnlVal for later use.
	//
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(padapter, (RF_PATH)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(padapter, (RF_PATH)1, RF_CHNLBW, bRFRegOffsetMask);

	
	//if (!pHalData->bMACFuncEnable) {
		_InitQueueReservedPage(padapter);
		_InitTxBufferBoundary(padapter);

		// init LLT after tx buffer boundary is defined
		ret = rtl8188f_InitLLTTable(padapter);
		if (_SUCCESS != ret)
		{
			DBG_8192C("%s: Failed to init LLT Table!\n", __FUNCTION__);
			return _FAIL;
		}
	//}
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize(padapter);

	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(padapter, DRVINFO_SZ);
	hal_init_macaddr(padapter);
	_InitNetworkType(padapter);
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	//_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
	_initSdioAggregationSetting(padapter);
	_InitOperationMode(padapter);
	rtl8188f_InitBeaconParameters(padapter);
	rtl8188f_InitBeaconMaxError(padapter, _TRUE);
	_InitInterrupt(padapter);
	_InitBurstPktLen_8188FS(padapter);

	//YJ,TODO
	rtw_write8(padapter, REG_SECONDARY_CCA_CTRL_8188F, 0x3);	// CCA 
	rtw_write8(padapter, 0x976, 0);	// hpfan_todo: 2nd CCA related

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	// Enable lifetime check for the four ACs
	rtw_write8(padapter, REG_LIFETIME_CTRL, rtw_read8(padapter, REG_LIFETIME_CTRL) | 0x0F);
#endif	// CONFIG_CHECK_AC_LIFETIME

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
#else	// CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	// unit: 256us. 3s
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	// unit: 256us. 3s
#endif	// CONFIG_TX_MCAST2UNI
#endif	// CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI


	invalidate_cam_all(padapter);

	BBTurnOnBlock_8188F(padapter);

	rtw_hal_set_chnl_bw(padapter, padapter->registrypriv.channel,
		CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

	// Record original value for template. This is arough data, we can only use the data
	// for power adjust. The value can not be adjustde according to different power!!!
//	pHalData->OriginalCckTxPwrIdx = pHalData->CurrentCckTxPwrIdx;
//	pHalData->OriginalOfdm24GTxPwrIdx = pHalData->CurrentOfdm24GTxPwrIdx;

	rtl8188f_InitAntenna_Selection(padapter);

	//
	// Disable BAR, suggested by Scott
	// 2010.04.09 add by hpfan
	//
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	// HW SEQ CTRL
	// set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);


#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN);
	u1bTmp &= ~(FEN_BBRSTB|FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN,u1bTmp);

	rtw_write8(padapter, REG_RD_CTRL, 0x0F);
	rtw_write8(padapter, REG_RD_CTRL+1, 0xCF);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8188F, 0x80);
	rtw_write32(padapter, REG_CR, 0x0b0202ff);
#endif

	/*
	* onfigure SDIO TxRx Control to enable Rx DMA timer masking.
	* 2010.02.24.
	* Only clear necessary bits 0x0[2:0] and 0x2[15:0] and keep 0x0[15:3]
	* 2015.03.19.
	*/
	u4Tmp = rtw_read32(padapter, SDIO_LOCAL_BASE|SDIO_REG_TX_CTRL);
	#if 0
	u4Tmp &= 0xFFFFFFF8;
	#else
	u4Tmp &= 0x0000FFF8;
	#endif
	rtw_write32(padapter, SDIO_LOCAL_BASE|SDIO_REG_TX_CTRL, u4Tmp);

	_RfPowerSave(padapter);
	
	rtl8188f_InitHalDm(padapter);

	//DbgPrint("pHalData->DefaultTxPwrDbm = %d\n", pHalData->DefaultTxPwrDbm);

//	if(pHalData->SwBeaconType < HAL92CSDIO_DEFAULT_BEACON_TYPE) // The lowest Beacon Type that HW can support
//		pHalData->SwBeaconType = HAL92CSDIO_DEFAULT_BEACON_TYPE;

	//
	// Update current Tx FIFO page status.
	//
	HalQueryTxBufferStatus8188FSdio(padapter);
	HalQueryTxOQTBufferStatus8188FSdio(padapter);
	pHalData->SdioTxOQTMaxFreeSpace = pHalData->SdioTxOQTFreeSpace;

	// Enable MACTXEN/MACRXEN block
	u1bTmp = rtw_read8(padapter, REG_CR);
	u1bTmp |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bTmp);

	rtw_hal_set_hwreg(padapter, HW_VAR_NAV_UPPER, (u8*)&NavUpper);

#ifdef CONFIG_XMIT_ACK
	//ack for xmit mgmt frames.
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL)|BIT(12));
#endif //CONFIG_XMIT_ACK	

//	pHalData->PreRpwmVal = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HRPWM1) & 0x80;

#if (MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1)
	{
		padapter->mppriv.channel = pHalData->CurrentChannel;
		MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
	}
	else
#endif //#if (MP_DRIVER == 1)
	{
		pwrctrlpriv->rf_pwrstate = rf_on;

		if(pwrctrlpriv->rf_pwrstate == rf_on)
		{
			struct pwrctrl_priv *pwrpriv;
			u32 start_time;
			u8 restore_iqk_rst;
			u8 b2Ant;
			u8 h2cCmdBuf;

			pwrpriv = adapter_to_pwrctl(padapter);

			PHY_LCCalibrate_8188F(&pHalData->odmpriv);

			#ifdef CONFIG_BT_COEXIST
			/* Inform WiFi FW that it is the beginning of IQK */
			h2cCmdBuf = 1;
			FillH2CCmd8188F(padapter, H2C_8188F_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			start_time = rtw_get_current_time();
			do {
				if (rtw_read8(padapter, 0x1e7) & 0x01)
					break;

				rtw_msleep_os(50);
			} while (rtw_get_passing_time_ms(start_time) <= 400);
			#endif

			restore_iqk_rst = (pwrpriv->bips_processing==_TRUE)?_TRUE:_FALSE;
			PHY_IQCalibrate_8188F(padapter, _FALSE, restore_iqk_rst);
			pHalData->bIQKInitialized = _TRUE;

			#ifdef CONFIG_BT_COEXIST
			/* Inform WiFi FW that it is the finish of IQK */
			h2cCmdBuf = 0;
			FillH2CCmd8188F(padapter, H2C_8188F_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);
			#endif

			ODM_TXPowerTrackingCheck(&pHalData->odmpriv);
		}
	}


	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("-%s\n", __FUNCTION__));

	return _SUCCESS;
}

//
// Description:
//	RTL8188e card disable power sequence v003 which suggested by Scott.
//
// First created by tynli. 2011.01.28.
//
static void CardDisableRTL8188FSdio(PADAPTER padapter)
{
	u8		u1bTmp;
	u16		u2bTmp;
	u32		u4bTmp;
	u8		bMacPwrCtrlOn;
	u8 hci_sus_state;
	u8		ret = _FAIL;

	// Run LPS WL RFOFF flow
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8188F_enter_lps_flow);
	if (ret == _FAIL) {
		DBG_8192C(KERN_ERR "%s: run RF OFF flow fail!\n", __func__);
	}

	//	==== Reset digital sequence   ======

	u1bTmp = rtw_read8(padapter, REG_MCUFWDL);
	if ((u1bTmp & RAM_DL_SEL) && padapter->bFWReady) //8051 RAM code
		rtl8188f_FirmwareSelfReset(padapter);

	// Reset MCU 0x2[10]=0. Suggested by Filen. 2011.01.26. by tynli.
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	u1bTmp &= ~BIT(2);	// 0x2[10], FEN_CPUEN
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp);

	// MCUFWDL 0x80[1:0]=0
	// reset MCU ready status
	rtw_write8(padapter, REG_MCUFWDL, 0);

	// Reset MCU IO Wrapper, added by Roger, 2011.08.30

//derek mod for reg 0x1d
//	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
//	u1bTmp &= ~BIT(0);
//	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);
//	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
//	u1bTmp |= BIT(0);
//	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);

	//	==== Reset digital sequence end ======
	
	bMacPwrCtrlOn = _FALSE;	// Disable CMD53 R/W
	ret = _FALSE;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	hci_sus_state = HCI_SUS_ENTERING;
	rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8188F_card_disable_flow);
	if (ret == _FALSE) {
		hci_sus_state = HCI_SUS_ERR;
		rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
		DBG_8192C(KERN_ERR "%s: run CARD DISABLE flow fail!\n", __func__);
	} else {
		hci_sus_state = HCI_SUS_ENTER;
		rtw_hal_set_hwreg(padapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
	}

	padapter->bFWReady = _FALSE;
}

static u32 rtl8188fs_hal_deinit(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		MPT_DeInitAdapter(padapter);
#endif

	if (rtw_is_hw_init_completed(padapter)) {
#ifdef CONFIG_SWLPS_IN_IPS				
		if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE)
		{
			u8	bMacPwrCtrlOn;
			u8 ret =  _TRUE;

			DBG_871X("%s: run LPS flow in IPS\n", __FUNCTION__);

			rtw_write32(padapter, 0x130, 0x0);
			rtw_write32(padapter, 0x138, 0x100);
			rtw_write8(padapter, 0x13d, 0x1);


			bMacPwrCtrlOn = _FALSE;	// Disable CMD53 R/W	
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
			
			ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8188F_enter_swlps_flow);
			if (ret == _FALSE) {
				DBG_8192C("%s: run LPS flow in IPS fail!\n", __FUNCTION__);
				return _FAIL;
			}
		}
		else
#elif defined(CONFIG_FWLPS_IN_IPS)
		if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE)
		{
			if(padapter->netif_up == _TRUE)
			{
				int cnt=0;
				u8 val8 = 0;
			
				DBG_871X("%s: issue H2C to FW when entering IPS\n", __FUNCTION__);
			
				rtl8188f_set_FwPwrModeInIPS_cmd(padapter, 0x3);
				//poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc=0 means H2C done by FW.
				do{
					val8 = rtw_read8(padapter, REG_HMETFR);
					cnt++;
					DBG_871X("%s  polling REG_HMETFR=0x%x, cnt=%d \n", __FUNCTION__, val8, cnt);
					rtw_mdelay_os(10);
				}while(cnt<100 && (val8!=0));
				//H2C done, enter 32k
				if(val8 == 0)
				{
					//ser rpwm to enter 32k
					val8 = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1);
					val8 += 0x80;
					val8 |= BIT(0);
					rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, val8);
					DBG_871X("%s: write rpwm=%02x\n", __FUNCTION__, val8);
					adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
					cnt = val8 = 0;
					do{
						val8 = rtw_read8(padapter, REG_CR);
						cnt++;
						DBG_871X("%s  polling 0x100=0x%x, cnt=%d \n", __FUNCTION__, val8, cnt);			
						rtw_mdelay_os(10);
					}while(cnt<100 && (val8!=0xEA));
#ifdef DBG_CHECK_FW_PS_STATE
				if(val8 != 0xEA)
					DBG_871X("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
					, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
#endif //DBG_CHECK_FW_PS_STATE
				}
				else
				{
					DBG_871X("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
					, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
				}
			
				DBG_871X("polling done when entering IPS, check result : 0x100=0x%x, cnt=%d, MAC_1cc=0x%02x\n"
				, rtw_read8(padapter, REG_CR), cnt, rtw_read8(padapter, REG_HMETFR));

				adapter_to_pwrctl(padapter)->pre_ips_type = 0;
				
			}
			else
			{
				pdbgpriv->dbg_carddisable_cnt++;
#ifdef DBG_CHECK_FW_PS_STATE
				if(rtw_fw_ps_state(padapter) == _FAIL)
				{
					DBG_871X("card disable should leave 32k\n");
					pdbgpriv->dbg_carddisable_error_cnt++;
				}
#endif //DBG_CHECK_FW_PS_STATE
				rtw_hal_power_off(padapter);

				adapter_to_pwrctl(padapter)->pre_ips_type = 1;
			}
			
		}
		else
#endif //CONFIG_SWLPS_IN_IPS
		{
			pdbgpriv->dbg_carddisable_cnt++;
#ifdef DBG_CHECK_FW_PS_STATE
			if(rtw_fw_ps_state(padapter) == _FAIL)
			{
				DBG_871X("card disable should leave 32k\n");
				pdbgpriv->dbg_carddisable_error_cnt++;
			}
#endif //DBG_CHECK_FW_PS_STATE
			rtw_hal_power_off(padapter);
		}
	}
	else
	{
		pdbgpriv->dbg_deinit_fail_cnt++;
	}

	return _SUCCESS;
}
static void rtl8188fs_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	rtl8188f_init_default_value(padapter);

	// interface related variable
	pHalData->SdioRxFIFOCnt = 0;
}

static void rtl8188fs_interface_configure(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv		*pdvobjpriv = adapter_to_dvobj(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;


	pdvobjpriv->RtOutPipe[0] = WLAN_TX_HIQ_DEVICE_ID;
	pdvobjpriv->RtOutPipe[1] = WLAN_TX_MIQ_DEVICE_ID;
	pdvobjpriv->RtOutPipe[2] = WLAN_TX_LOQ_DEVICE_ID;

	if (bWiFiConfig)
		pHalData->OutEpNumber = 2;
	else
		pHalData->OutEpNumber = SDIO_MAX_TX_QUEUE;

	switch(pHalData->OutEpNumber){
		case 3:
			pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_LQ|TX_SELE_NQ;
			break;
		case 2:
			pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_NQ;
			break;
		case 1:
			pHalData->OutEpQueueSel=TX_SELE_HQ;
			break;
		default:
			break;
	}

	Hal_MappingOutPipe(padapter, pHalData->OutEpNumber);
}

//
//	Description:
//		We should set Efuse cell selection to WiFi cell in default.
//
//	Assumption:
//		PASSIVE_LEVEL
//
//	Added by Roger, 2010.11.23.
//
static void
_EfuseCellSel(
	IN	PADAPTER	padapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	u32			value32;

	//if(INCLUDE_MULTI_FUNC_BT(padapter))
	{
		value32 = rtw_read32(padapter, EFUSE_TEST);
		value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
		rtw_write32(padapter, EFUSE_TEST, value32);
	}
}

static VOID
_ReadRFType(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}

static VOID
_ReadEfuseInfo8188FS(
	IN PADAPTER			padapter
	)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8*			hwinfo = NULL;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("====>_ReadEfuseInfo8188FS()\n"));

	//
	// This part read and parse the eeprom/efuse content
	//

	if (sizeof(pHalData->efuse_eeprom_data) < HWSET_MAX_SIZE_8188F)
		DBG_871X("[WARNING] size of efuse_eeprom_data is less than HWSET_MAX_SIZE_8188F!\n");

	hwinfo = pHalData->efuse_eeprom_data;

	Hal_InitPGData(padapter, hwinfo);

	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParseEEPROMVer_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_config_macaddr(padapter, pHalData->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	//Hal_EfuseParseBoardType_8188FS(padapter, hwinfo, pHalData->bautoload_fail_flag);

	//
	// Read Bluetooth co-exist and initialize
	//
	//Hal_EfuseParseBTCoexistInfo_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseChnlPlan_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseCustomerID_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	
	//Hal_EfuseParseVoltage_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	Hal_DetectWoWMode(padapter);
#endif

	Hal_EfuseParseKFreeData_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_read_mac_hidden_rpt(padapter);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<==== _ReadEfuseInfo8188FS()\n"));
}

static void _ReadPROMContent(
	IN PADAPTER 		padapter
	)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			eeValue;

	eeValue = rtw_read8(padapter, REG_9346CR);
	// To check system boot selection.
	pHalData->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pHalData->bautoload_fail_flag = (eeValue & EEPROM_EN) ? _FALSE : _TRUE;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("%s: 9346CR=0x%02X, Boot from %s, Autoload %s\n",
		  __FUNCTION__, eeValue,
		  (pHalData->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		  (pHalData->bautoload_fail_flag ? "Fail" : "OK")));

//	pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE;

	_ReadEfuseInfo8188FS(padapter);
}

//
//	Description:
//		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported.
//
//	Assumption:
//		PASSIVE_LEVEL (SDIO interface)
//
//
static void ReadAdapterInfo8188FS(PADAPTER padapter)
{
	/* Read EEPROM size before call any EEPROM function */
	padapter->EepromAddressSize = GetEEPROMSize8188F(padapter);

	_EfuseCellSel(padapter);
	_ReadRFType(padapter);
	_ReadPROMContent(padapter);
}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8188F()
 */
void SetHwReg8188FS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData;
	u8 val8;

_func_enter_;

	pHalData = GET_HAL_DATA(padapter);

	switch (variable)
	{
		case HW_VAR_SET_RPWM:
			// rpwm value only use BIT0(clock bit) ,BIT6(Ack bit), and BIT7(Toggle bit)
			// BIT0 value - 1: 32k, 0:40MHz.
			// BIT6 value - 1: report cpwm value after success set, 0:do not report.
			// BIT7 value - Toggle bit change.
			{
				val8 = *val;
				val8 &= 0xC1;
				rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, val8);
			}
			break;
		case HW_VAR_SET_REQ_FW_PS:
			//1. driver write 0x8f[4]=1  //request fw ps state (only can write bit4)
			{
				u8 req_fw_ps=0;
				req_fw_ps = rtw_read8(padapter, 0x8f);
				req_fw_ps |= 0x10;
				rtw_write8(padapter, 0x8f, req_fw_ps);
			}
			break;
		case HW_VAR_RXDMA_AGG_PG_TH:
			val8 = *val;

			// TH=1 => invalidate RX DMA aggregation
			// TH=0 => validate RX DMA aggregation, use init value.
			if (val8 == 0)
			{
				// enable RXDMA aggregation
				//_RXAggrSwitch(padapter, _TRUE);
			}
			else
			{
				// disable RXDMA aggregation
				//_RXAggrSwitch(padapter, _FALSE);
			}
			break;
		case HW_VAR_DM_IN_LPS:
			rtl8188f_hal_dm_in_lps(padapter);
			break;
		default:
			SetHwReg8188F(padapter, variable, val);
			break;
	}

_func_exit_;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8188F()
 */
void GetHwReg8188FS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

_func_enter_;

	switch (variable)
	{
		case HW_VAR_CPWM:
			*val = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HCPWM1_8188F);
			break;

		case HW_VAR_FW_PS_STATE:
			{
				//3. read dword 0x88               //driver read fw ps state
				*((u16*)val) = rtw_read16(padapter, 0x88);
			}
			break;
		default:
			GetHwReg8188F(padapter, variable, val);
			break;
	}

_func_exit_;
}

//
//	Description:
//		Query setting of specified variable.
//
u8
GetHalDefVar8188FSDIO(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*((u8 *)pValue) = _FALSE;
#endif
			break;
		case HAL_DEF_CURRENT_ANTENNA:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*(( u8*)pValue) = pHalData->CurAntenna;
#endif
			break;
		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			// Stanley@BB.SD3 suggests 16K can get stable performance
			// coding by Lucas@20130730
			*(HT_CAP_AMPDU_FACTOR*)pValue = MAX_AMPDU_FACTOR_16K;
			break;
		default:
			bResult = GetHalDefVar8188F(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}

//
//	Description:
//		Change default setting of specified variable.
//
u8
SetHalDefVar8188FSDIO(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		default:
			bResult = SetHalDefVar8188F(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}

void rtl8188fs_set_hal_ops(PADAPTER padapter)
{
	struct hal_ops *pHalFunc = &padapter->HalFunc;

_func_enter_;

	rtl8188f_set_hal_ops(pHalFunc);

	pHalFunc->hal_power_on = &_InitPowerOn_8188FS;
	pHalFunc->hal_power_off = &CardDisableRTL8188FSdio;

	pHalFunc->hal_init = &rtl8188fs_hal_init;
	pHalFunc->hal_deinit = &rtl8188fs_hal_deinit;

	pHalFunc->init_xmit_priv = &rtl8188fs_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188fs_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188fs_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188fs_free_recv_priv;
#ifdef CONFIG_RECV_THREAD_MODE
	pHalFunc->recv_hdl = rtl8188fs_recv_hdl;
#endif

	pHalFunc->InitSwLeds = &rtl8188fs_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188fs_DeInitSwLeds;

	pHalFunc->init_default_value = &rtl8188fs_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188fs_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188FS;

	pHalFunc->enable_interrupt = &EnableInterrupt8188FSdio;
	pHalFunc->disable_interrupt = &DisableInterrupt8188FSdio;
	pHalFunc->check_ips_status = &CheckIPSStatus;

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	pHalFunc->clear_interrupt = &ClearInterrupt8188FSdio;
#endif
	pHalFunc->SetHwRegHandler = &SetHwReg8188FS;
	pHalFunc->GetHwRegHandler = &GetHwReg8188FS;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8188FSDIO;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188FSDIO;

	pHalFunc->hal_xmit = &rtl8188fs_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188fs_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8188fs_hal_xmitframe_enqueue;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = NULL;
#endif

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	pHalFunc->hal_init_checkbthang_workqueue = &rtl8188fs_init_checkbthang_workqueue;
	pHalFunc->hal_free_checkbthang_workqueue = &rtl8188fs_free_checkbthang_workqueue;
	pHalFunc->hal_cancle_checkbthang_workqueue = &rtl8188fs_cancle_checkbthang_workqueue;
	pHalFunc->hal_checke_bt_hang = &rtl8188fs_hal_check_bt_hang;
#endif

_func_exit_;
}


