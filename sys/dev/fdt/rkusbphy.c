/*
 * Copyright (c) 2021 Jordan Yelloz <jordan@yelloz.me>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <arm64/dev/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct rkusbphy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int                     sc_node;

	int                     n_host_intr;
	void *                 *host_intr;
	int                     n_otg_intr;
	void *                 *otg_intr;

	struct phy_device	sc_pd;
};

int rkusbphy_match(struct device *, void *, void *);
void rkusbphy_attach(struct device *, struct device *, void *);
void rkusbphy_register_host_interrupts(struct rkusbphy_softc *);
void rkusbphy_register_otg_interrupts(struct rkusbphy_softc *);
int rkusbphy_enable(void *, uint32_t *);

struct cfattach	rkusbphy_ca = {
	sizeof (struct rkusbphy_softc), rkusbphy_match, rkusbphy_attach
};

struct cfdriver rkusbphy_cd = {
	NULL, "rkusbphy", DV_DULL
};

int	rkusbphy_host_intr(void *);
int	rkusbphy_otg_intr(void *);

int
rkusbphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3328-usb2phy");
}

void
rkusbphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkusbphy_softc *sc = (struct rkusbphy_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf(": %llx, %llx", faa->fa_reg[0].addr, faa->fa_reg[0].size);

	power_domain_enable(faa->fa_node);
	clock_enable_all(faa->fa_node);

	sc->sc_node = faa->fa_node;

	rkusbphy_register_host_interrupts(sc);
	rkusbphy_register_otg_interrupts(sc);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_enable = rkusbphy_enable;

	phy_register(&sc->sc_pd);

	printf("\n");
}

void
rkusbphy_register_host_interrupts(struct rkusbphy_softc *sc)
{
	int child_node;
	int n_interrupts;
	int idx;

	child_node = OF_getnodebyname(sc->sc_node, "host-port");
	if (child_node <= 0) {
		printf(": no host-port child node\n");
		return;
	}

	n_interrupts = OF_getproplen(child_node, "interrupts");
	if (n_interrupts < 1) {
		printf(": no host-port interrupts to enable\n");
		return;
	}

	sc->n_host_intr = n_interrupts;
	sc->host_intr = malloc(
			sizeof(void *) * n_interrupts,
			M_DEVBUF,
			M_WAITOK
	);

	for (idx = 0; idx < n_interrupts; idx++) {
		sc->host_intr[idx] = fdt_intr_establish_idx(
			child_node,
			idx,
			IPL_BIO,
			rkusbphy_host_intr,
			sc,
			"rkuhost"
		);
	}
}

void
rkusbphy_register_otg_interrupts(struct rkusbphy_softc *sc)
{
	int child_node;
	int n_interrupts;
	int idx;

	child_node = OF_getnodebyname(sc->sc_node, "otg-port");
	if (child_node <= 0) {
		printf(": no otg-port child node\n");
		return;
	}

	n_interrupts = OF_getproplen(child_node, "interrupts");
	if (n_interrupts < 1) {
		printf(": no otg-port interrupts to enable\n");
		return;
	}

	sc->n_otg_intr = n_interrupts;
	sc->otg_intr = malloc(
			sizeof(void *) * n_interrupts,
			M_DEVBUF,
			M_WAITOK
	);

	for (idx = 0; idx < n_interrupts; idx++) {
		sc->otg_intr[idx] = fdt_intr_establish_idx(
			child_node,
			idx,
			IPL_BIO,
			rkusbphy_otg_intr,
			sc,
			"rkuotg"
		);
	}
}

int
rkusbphy_enable(void *cookie, uint32_t *cells)
{
	return 0;
}

int
rkusbphy_host_intr(void *arg)
{
	printf("rkusbphy: handling host interrupt\n");
	return 1;
}

int
rkusbphy_otg_intr(void *arg)
{
	printf("rkusbphy: handling otg interrupt\n");
	return 1;
}
