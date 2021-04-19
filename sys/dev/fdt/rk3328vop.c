/* $OpenBSD: rk3328vop.c,v 1.4 2020/06/08 04:47:58 jsg Exp $ */
/* $NetBSD: rk_vop.c,v 1.6 2020/01/05 12:14:35 mrg Exp $ */

/*-
 * Copyright (c) 2019 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>

#include <dev/fdt/rkdrm.h>

struct rk3328vop_softc;

struct rk3328vop_crtc {
	struct drm_crtc		base;
	struct rk3328vop_softc	*sc;
};

struct rk3328vop_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct rk3328vop_crtc	sc_crtc;
	struct drm_plane	sc_plane;
	struct device_ports	sc_ports;
};

int rk3328vop_match(struct device *, void *, void *);
void rk3328vop_attach(struct device *, struct device *, void *);

struct cfattach	rk3328vop_ca = {
	sizeof (struct rk3328vop_softc), rk3328vop_match, rk3328vop_attach
};

#define	VOP_WIN0_VIR			0x003c
#define	VOP_WIN0_YRGB_MST		0x0040
#define	VOP_WIN0_DSP_INFO		0x004c

#define	HREAD4(sc, reg)				\
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg))

struct cfdriver rk3328vop_cd = {
	NULL, "rk3328vop", DV_DULL
};

int
rk3328vop_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3328-vop"));
}

void
rk3328vop_attach(struct device *parent, struct device *self, void *aux)
{
	struct rk3328vop_softc *sc = (struct rk3328vop_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i, port, ep, nep;
	paddr_t paddr;

	if (faa->fa_nreg < 1)
		return;

	clock_set_assigned(faa->fa_node);

	reset_deassert(faa->fa_node, "axi");
	reset_deassert(faa->fa_node, "ahb");
	reset_deassert(faa->fa_node, "dclk");

	clock_enable(faa->fa_node, "aclk_vop");
	clock_enable(faa->fa_node, "hclk_vop");
	clock_enable(faa->fa_node, "dclk_vop");

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	sc->sc_node = faa->fa_node;

	printf("fake rk3328 vop\n");

	sc->sc_ports.dp_node = faa->fa_node;
	sc->sc_ports.dp_cookie = sc;
	device_ports_register(&sc->sc_ports, EP_DRM_CRTC);

	paddr = HREAD4(sc, VOP_WIN0_YRGB_MST);
	if (paddr != 0) {
		uint32_t stride, height;

		stride = HREAD4(sc, VOP_WIN0_VIR) & 0xffff;
		height = (HREAD4(sc, VOP_WIN0_DSP_INFO) >> 16) + 1;
		rasops_claim_framebuffer(paddr, height * stride * 4, self);
	}
}
