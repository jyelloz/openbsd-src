/* $OpenBSD: rk3328dwhdmi.c,v 1.5 2020/06/30 02:19:11 deraadt Exp $ */
/* $NetBSD: rk_dwhdmi.c,v 1.4 2019/12/17 18:26:36 jakllsch Exp $ */

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
#include <sys/kernel.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <drm/drm_crtc_helper.h>

#include <dev/ic/dwhdmi.h>

struct rk3328dwhdmi_softc {
	struct dwhdmi_softc	sc_base;
	int			sc_node;

	struct drm_display_mode	sc_curmode;
	struct drm_encoder	sc_encoder;
	struct regmap		*sc_grf;

	int			sc_activated;

	struct device_ports	sc_ports;
};

#define	to_rk3328dwhdmi_softc(x)	container_of(x, struct rk3328dwhdmi_softc, sc_base)
#define	to_rk3328dwhdmi_encoder(x)	container_of(x, struct rk3328dwhdmi_softc, sc_encoder)

int rk3328dwhdmi_match(struct device *, void *, void *);
void rk3328dwhdmi_attach(struct device *, struct device *, void *);

void rk3328dwhdmi_encoder_enable(struct drm_encoder *);

int rk3328dwhdmi_ep_activate(void *, struct endpoint *, void *);
void *rk3328dwhdmi_ep_get_cookie(void *, struct endpoint *);

void rk3328dwhdmi_enable(struct dwhdmi_softc *);
enum drm_mode_status rk3328dwhdmi_mode_valid(struct dwhdmi_softc *,
    const struct drm_display_mode *);

struct cfattach	rk3328dwhdmi_ca = {
	sizeof (struct rk3328dwhdmi_softc), rk3328dwhdmi_match, rk3328dwhdmi_attach
};

struct cfdriver rk3328dwhdmi_cd = {
	NULL, "rk3328dwhdmi", DV_DULL
};

int
rk3328dwhdmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	return OF_is_compatible(faa->fa_node, "rockchip,rk3328-dw-hdmi");
}

const struct dwhdmi_mpll_config rk3328dwhdmi_mpll_config[] = {
	{ 0,		0x0051, 0x0003, 0x0000 },
};

const struct dwhdmi_phy_config rk3328dwhdmi_phy_config[] = {
	{ 0,		0x0000, 0x0000, 0x0000 }
};

void
rk3328dwhdmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct rk3328dwhdmi_softc *sc = (struct rk3328dwhdmi_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t grf;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t phandle;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	pinctrl_byname(sc->sc_node, "default");

	clock_enable(faa->fa_node, "iahb");
	clock_enable(faa->fa_node, "isfr");
	clock_enable(faa->fa_node, "cec");

	sc->sc_base.sc_reg_width =
	    OF_getpropint(faa->fa_node, "reg-io-width", 4);

	sc->sc_base.sc_bst = faa->fa_iot;
	if (bus_space_map(sc->sc_base.sc_bst, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_base.sc_bsh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;

	grf = OF_getpropint(faa->fa_node, "rockchip,grf", 0);
	sc->sc_grf = regmap_byphandle(grf);
	if (sc->sc_grf == NULL) {
		printf(": can't get grf\n");
		return;
	}

	printf(": HDMI TX\n");

	sc->sc_base.sc_flags |= DWHDMI_USE_INTERNAL_PHY;
	sc->sc_base.sc_detect = dwhdmi_phy_detect;
	sc->sc_base.sc_enable = rk3328dwhdmi_enable;
	sc->sc_base.sc_disable = dwhdmi_phy_disable;
	sc->sc_base.sc_mode_set = dwhdmi_phy_mode_set;
	sc->sc_base.sc_mode_valid = rk3328dwhdmi_mode_valid;
	sc->sc_base.sc_mpll_config = rk3328dwhdmi_mpll_config;
	sc->sc_base.sc_phy_config = rk3328dwhdmi_phy_config;

	if (dwhdmi_attach(&sc->sc_base) != 0) {
		printf("%s: failed to attach driver\n", self->dv_xname);
		return;
	}

	sc->sc_ports.dp_node = faa->fa_node;
	sc->sc_ports.dp_cookie = sc;
	sc->sc_ports.dp_ep_activate = rk3328dwhdmi_ep_activate;
	sc->sc_ports.dp_ep_get_cookie = rk3328dwhdmi_ep_get_cookie;

	// device_ports_register(&sc->sc_ports, EP_DRM_ENCODER);

}

void
rk3328dwhdmi_enable(struct dwhdmi_softc *dsc)
{
	dwhdmi_phy_enable(dsc);
}

struct drm_encoder_funcs rk3328dwhdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

struct drm_encoder_helper_funcs rk3328dwhdmi_encoder_helper_funcs = {
	.enable = rk3328dwhdmi_encoder_enable,
};

int
rk3328dwhdmi_ep_activate(void *cookie, struct endpoint *ep, void *arg)
{
	struct rk3328dwhdmi_softc *sc = cookie;
	struct drm_crtc *crtc = NULL;
	struct endpoint *rep;
	int error;

	if (sc->sc_activated)
		return 0;

	rep = endpoint_remote(ep);
	if (rep && rep->ep_type == EP_DRM_CRTC)
		crtc = endpoint_get_cookie(rep);
	if (crtc == NULL)
		return EINVAL;

	sc->sc_encoder.possible_crtcs = 0x1;
	drm_encoder_init(crtc->dev, &sc->sc_encoder, &rk3328dwhdmi_encoder_funcs, DRM_MODE_ENCODER_TMDS, NULL);
	sc->sc_base.sc_connector.base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	error = dwhdmi_bind(&sc->sc_base, &sc->sc_encoder);
	if (error != 0)
		return error;

	sc->sc_activated = 1;

	return 0;
}

void *
rk3328dwhdmi_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	struct rk3328dwhdmi_softc *sc = cookie;
	return &sc->sc_encoder;
}

void
rk3328dwhdmi_encoder_enable(struct drm_encoder *encoder)
{
}

enum drm_mode_status
rk3328dwhdmi_mode_valid(struct dwhdmi_softc *dsc, const struct drm_display_mode *mode)
{
	return MODE_OK;
}
