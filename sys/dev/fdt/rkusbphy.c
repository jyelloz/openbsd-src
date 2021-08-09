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
#include <sys/kthread.h>

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
	struct regmap		*sc_rm;
	bus_size_t		sc_off;

	int			sc_node;

	void			*host_intr;
	void			*otg_intr;

	struct phy_device	host_port;
	struct phy_device	otg_port;
};

int rkusbphy_match(struct device *, void *, void *);
void rkusbphy_attach(struct device *, struct device *, void *);
void rkusbphy_register_host_interrupts(struct rkusbphy_softc *);
void rkusbphy_register_otg_interrupts(struct rkusbphy_softc *);
void rkusbphy_deferred(void *);
int rkusbphy_enable(void *, uint32_t *);

struct cfattach	rkusbphy_ca = {
	sizeof (struct rkusbphy_softc), rkusbphy_match, rkusbphy_attach
};

struct cfdriver rkusbphy_cd = {
	NULL, "rkusbphy", DV_DULL
};

int	rkusbphy_intr(void *);

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

	sc->sc_node = faa->fa_node;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}
	sc->sc_off = faa->fa_reg[0].addr;

	sc->sc_rm = regmap_bynode(OF_parent(sc->sc_node));
	if (sc->sc_rm == NULL) {
		printf(": can't map registers\n");
		return;
	}

	power_domain_enable(faa->fa_node);
	clock_enable_all(faa->fa_node);

	rkusbphy_register_host_interrupts(sc);
	rkusbphy_register_otg_interrupts(sc);

	kthread_create_deferred(rkusbphy_deferred, sc);
}

#define LINESTATE_IRQ_ENABLE	0x110UL
#define LINESTATE_IRQ_STATE	0x114UL
#define LINESTATE_IRQ_CLEAR	0x118UL
void
rkusbphy_deferred(void *const self)
{
	struct rkusbphy_softc *const sc = (struct rkusbphy_softc *) self;
	uint32_t const old_reg = regmap_read_4(sc->sc_rm, LINESTATE_IRQ_ENABLE);
	uint32_t const new_reg = old_reg | (
		(1U << 0) // OTG Linestate
		|
		(1U << 1) // HOST Linestate
		|
		(1U << 2) // "BVALID"
		|
		(1U << 4) // OTG ID Rise
		|
		(1U << 5) // OTG ID Fall
	);
	/* XXX: Known to kind of work */
	//regmap_write_4(sc->sc_rm, LINESTATE_IRQ_ENABLE, 0xffffffff);
	/* not confirmed to work yet */
	regmap_write_4(sc->sc_rm, LINESTATE_IRQ_ENABLE, new_reg);
	printf(
		"%s: irq enable old=%#x, new=%#x\n",
		sc->sc_dev.dv_xname,
		old_reg,
		new_reg
	);
}

void *
rkusbphy_register_linestate_interrupt(
	struct rkusbphy_softc *sc,
	char const *node_name
)
{
	int child_node;
	int linestate_idx;
	void *interrupt;

	child_node = OF_getnodebyname(sc->sc_node, node_name);
	if (child_node <= 0) {
		printf(": no %s child node\n", node_name);
		return NULL;
	}

	linestate_idx = OF_getindex(child_node, "linestate", "interrupt-names");
	if (linestate_idx < 0) {
		printf(": %s no linestate interrupts to enable\n", node_name);
		return NULL;
	}
	interrupt = fdt_intr_establish_idx(
		child_node,
		linestate_idx,
		IPL_BIO,
		rkusbphy_intr,
		sc,
		sc->sc_dev.dv_xname
	);
	if (interrupt == NULL) {
		printf(": unable to establish linestate interrupt@%d\n", linestate_idx);
		return NULL;
	}

	printf(": intr=%p", interrupt);

	return interrupt;
}

void
rkusbphy_register_host_interrupts(struct rkusbphy_softc *sc)
{
	int const child_node = OF_getnodebyname(sc->sc_node, "host-port");
	sc->host_intr = rkusbphy_register_linestate_interrupt(sc, "host-port");
	sc->host_port.pd_node = child_node;
	sc->host_port.pd_cookie = sc;
	sc->host_port.pd_enable = rkusbphy_enable;
	phy_register(&sc->host_port);
}

void
rkusbphy_register_otg_interrupts(struct rkusbphy_softc *sc)
{
	int const child_node = OF_getnodebyname(sc->sc_node, "otg-port");
	sc->otg_intr = rkusbphy_register_linestate_interrupt(sc, "otg-port");
	sc->otg_port.pd_node = child_node;
	sc->otg_port.pd_cookie = sc;
	sc->otg_port.pd_enable = rkusbphy_enable;
	phy_register(&sc->otg_port);
}

int
rkusbphy_enable(void *cookie, uint32_t *cells)
{
	//struct rkusbphy_softc *sc = (struct rkusbphy_softc *) cookie;
	//printf("%s: enabling PHY\n", sc->sc_dev.dv_xname);
	return 0;
}

int
rkusbphy_intr(void *arg)
{
	uint32_t reg;
	struct rkusbphy_softc *sc = (struct rkusbphy_softc *) arg;
	reg = regmap_read_4(sc->sc_rm, LINESTATE_IRQ_STATE);
	printf(
		"%s: handling linestate interrupt, state=%#x clearing flags\n",
		sc->sc_dev.dv_xname,
		reg
	);
	reg = (
		(1U << 0) // OTG Linestate
		|
		(1U << 1) // HOST Linestate
		|
		(1U << 2) // "BVALID"
		|
		(1U << 4) // OTG ID Rise
		|
		(1U << 5) // OTG ID Fall
	);
	regmap_write_4(sc->sc_rm, LINESTATE_IRQ_CLEAR, reg);
	return 1;
}
