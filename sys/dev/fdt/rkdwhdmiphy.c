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

#define __driver_prefix rkdwhdmiphy

struct rkdwhdmiphy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int rkdwhdmiphy_match(struct device *, void *, void *);
void rkdwhdmiphy_attach(struct device *, struct device *, void *);

struct cfattach	rkdwhdmiphy_ca = {
	sizeof (struct rkdwhdmiphy_softc), rkdwhdmiphy_match, rkdwhdmiphy_attach
};

struct cfdriver rkdwhdmiphy_cd = {
	NULL, "rkdwhdmiphy", DV_DULL
};

int	rkdwhdmiphy_enable(void *, uint32_t *);

int
rkdwhdmiphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3328-hdmi-phy");
}

void
rkdwhdmiphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;

	printf(": %llx, %llx\n", faa->fa_reg[0].addr, faa->fa_reg[0].size);

	power_domain_enable(faa->fa_node);
	clock_enable_all(faa->fa_node);
}

int
rkdwhdmiphy_enable(void *cookie, uint32_t *cells)
{
	return 0;
}
