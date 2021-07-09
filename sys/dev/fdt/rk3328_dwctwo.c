/*	$OpenBSD: rk3328_dwctwo.c,v 1.2 2021/02/05 00:42:25 patrick Exp $	*/
/*
 * Copyright (c) 2015 Masao Uebayashi <uebayasi@tombiinc.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/kthread.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/usb_quirks.h>

#include <dev/usb/dwc2/dwc2var.h>
#include <dev/usb/dwc2/dwc2.h>
#include <dev/usb/dwc2/dwc2_core.h>

#ifdef __armv7__
#include <arm/simplebus/simplebusvar.h>
#else
#include <arm64/dev/simplebusvar.h>
#endif

struct rk3328_dwctwo_softc {
	struct dwc2_softc	sc_dwc2;
	int			sc_node;
	void			*sc_ih;
};

int	rk3328_dwctwo_match(struct device *, void *, void *);
void	rk3328_dwctwo_attach(struct device *, struct device *, void *);
void	rk3328_dwctwo_deferred(void *);
void	rk3328_dwctwo_set_params(struct dwc2_core_params *);

extern void dwc2_set_all_params(struct dwc2_core_params *, int);

const struct cfattach rk3328_dwctwo_ca = {
	sizeof(struct rk3328_dwctwo_softc), rk3328_dwctwo_match, rk3328_dwctwo_attach,
};

struct cfdriver dwctwo_cd = {
	NULL, "dwctwo", DV_DULL
};

static struct dwc2_core_params rk3328_dwctwo_params = { -1 };

int
rk3328_dwctwo_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = (struct fdt_attach_args *)aux;
	return OF_is_compatible(faa->fa_node, "rockchip,rk3328-usb");
}

void
rk3328_dwctwo_attach(struct device *parent, struct device *self, void *aux)
{
	struct rk3328_dwctwo_softc *sc = (struct rk3328_dwctwo_softc *)self;
	struct fdt_attach_args *faa = aux;
	int idx;

	rk3328_dwctwo_set_params(&rk3328_dwctwo_params);

	sc->sc_node = faa->fa_node;
	sc->sc_dwc2.sc_iot = faa->fa_iot;
	sc->sc_dwc2.sc_bus.pipe_size = sizeof(struct usbd_pipe);
	sc->sc_dwc2.sc_bus.dmatag = faa->fa_dmat;
	sc->sc_dwc2.sc_params = &rk3328_dwctwo_params;

	if (bus_space_map(faa->fa_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_dwc2.sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_USB,
	    dwc2_intr, (void *)&sc->sc_dwc2, sc->sc_dwc2.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL)
		panic("%s: intr_establish failed!", __func__);

	idx = OF_getindex(sc->sc_node, "usb2-phy", "phy-names");
	if (idx < 0) {
		printf(", no PHYs to enable");
	} else if (phy_enable_idx(sc->sc_node, idx) != ENXIO) {
		printf(", failed to enable PHY@%d\n", idx);
		return;
	}
	printf("\n");

	kthread_create_deferred(rk3328_dwctwo_deferred, sc);
}

void
rk3328_dwctwo_deferred(void *self)
{
	struct rk3328_dwctwo_softc *sc = (struct rk3328_dwctwo_softc *)self;
	int rc;

	strlcpy(sc->sc_dwc2.sc_vendor, "Rockchip",
	    sizeof(sc->sc_dwc2.sc_vendor));

	rc = dwc2_init(&sc->sc_dwc2);
	if (rc != 0) {
		printf("%s: dwc2_init failed, rc=%d\n", __func__, rc);
		return;
	}

	sc->sc_dwc2.sc_child = config_found(&sc->sc_dwc2.sc_bus.bdev,
	    &sc->sc_dwc2.sc_bus, usbctlprint);
}

void
rk3328_dwctwo_set_params(struct dwc2_core_params *params)
{
	dwc2_set_all_params(params, -1);

	params->dma_desc_enable			= 0;
	params->otg_cap				= 2;	/* not HNP/SRP capable */
	params->host_rx_fifo_size		= 280;
	params->host_nperio_tx_fifo_size	= 16;
	params->host_perio_tx_fifo_size		= 256;
	params->ahbcfg				= 7 << 1;
}
