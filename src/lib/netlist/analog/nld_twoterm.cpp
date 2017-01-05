// license:GPL-2.0+
// copyright-holders:Couriersud
/*
 * nld_twoterm.c
 *
 */

#include <solver/nld_solver.h>
#include <algorithm>

#include "nld_twoterm.h"

namespace netlist
{
	namespace devices
	{
// ----------------------------------------------------------------------------------------
// generic_diode
// ----------------------------------------------------------------------------------------

generic_diode::generic_diode(device_t &dev, pstring name)
	: m_Vd(dev, name + ".m_Vd", 0.7)
	, m_Id(dev, name + ".m_Id", 0.0)
	, m_G(dev,  name + ".m_G", 1e-15)
	, m_Vt(0.0)
	, m_Is(0.0)
	, m_n(0.0)
	, m_gmin(1e-15)
	, m_VtInv(0.0)
	, m_Vcrit(0.0)
{
	set_param(1e-15, 1, 1e-15);
}

void generic_diode::set_param(const nl_double Is, const nl_double n, nl_double gmin)
{
	static const double csqrt2 = std::sqrt(2.0);
	m_Is = Is;
	m_n = n;
	m_gmin = gmin;

	m_Vt = 0.0258 * m_n;

	m_Vcrit = m_Vt * std::log(m_Vt / m_Is / csqrt2);
	m_VtInv = 1.0 / m_Vt;
}

void generic_diode::update_diode(const nl_double nVd)
{
#if 1
	if (nVd < NL_FCONST(-5.0) * m_Vt)
	{
		m_Vd = nVd;
		m_G = m_gmin;
		m_Id = - m_Is;
	}
	else if (nVd < m_Vcrit)
	{
		m_Vd = nVd;
		//m_Vd = m_Vd + 10.0 * m_Vt * std::tanh((nVd - m_Vd) / 10.0 / m_Vt);
		const nl_double eVDVt = std::exp(m_Vd * m_VtInv);
		m_Id = m_Is * (eVDVt - NL_FCONST(1.0));
		m_G = m_Is * m_VtInv * eVDVt + m_gmin;
	}
	else
	{
#if 1
		const nl_double a = std::max((nVd - m_Vd) * m_VtInv, NL_FCONST(-0.99));
		m_Vd = m_Vd + std::log1p(a) * m_Vt;
#else
		m_Vd = m_Vd + 10.0 * m_Vt * std::tanh((nVd - m_Vd) / 10.0 / m_Vt);
#endif
		const nl_double eVDVt = std::exp(m_Vd * m_VtInv);
		m_Id = m_Is * (eVDVt - NL_FCONST(1.0));

		m_G = m_Is * m_VtInv * eVDVt + m_gmin;
	}
#else
	m_Vd = m_Vd + 20.0 * m_Vt * std::tanh((nVd - m_Vd) / 20.0 / m_Vt);
	const nl_double eVDVt = std::exp(m_Vd * m_VtInv);
	m_Id = m_Is * (eVDVt - NL_FCONST(1.0));
	m_G = m_Is * m_VtInv * eVDVt + m_gmin;
#endif
}

// ----------------------------------------------------------------------------------------
// nld_twoterm
// ----------------------------------------------------------------------------------------

NETLIB_UPDATE(twoterm)
{
	/* only called if connected to a rail net ==> notify the solver to recalculate */
	/* we only need to call the non-rail terminal */
	if (m_P.has_net() && !m_P.net().isRailNet())
		m_P.schedule_solve();
	else if (m_N.has_net() && !m_N.net().isRailNet())
		m_N.schedule_solve();
}

// ----------------------------------------------------------------------------------------
// nld_R_base
// ----------------------------------------------------------------------------------------

NETLIB_RESET(R_base)
{
	NETLIB_NAME(twoterm)::reset();
	set_R(1.0 / netlist().gmin());
}

NETLIB_UPDATE(R_base)
{
	NETLIB_NAME(twoterm)::update();
}

// ----------------------------------------------------------------------------------------
// nld_R
// ----------------------------------------------------------------------------------------

NETLIB_UPDATE_PARAM(R)
{
	update_dev();
	if (m_R() > 1e-9)
		set_R(m_R());
	else
		set_R(1e-9);
}

// ----------------------------------------------------------------------------------------
// nld_POT
// ----------------------------------------------------------------------------------------

NETLIB_UPDATE_PARAM(POT)
{
	nl_double v = m_Dial();
	if (m_DialIsLog())
		v = (std::exp(v) - 1.0) / (std::exp(1.0) - 1.0);

	m_R1.update_dev();
	m_R2.update_dev();

	m_R1.set_R(std::max(m_R() * v, netlist().gmin()));
	m_R2.set_R(std::max(m_R() * (NL_FCONST(1.0) - v), netlist().gmin()));

}

// ----------------------------------------------------------------------------------------
// nld_POT2
// ----------------------------------------------------------------------------------------

NETLIB_UPDATE_PARAM(POT2)
{
	nl_double v = m_Dial();

	if (m_DialIsLog())
		v = (std::exp(v) - 1.0) / (std::exp(1.0) - 1.0);
	if (m_Reverse())
		v = 1.0 - v;

	m_R1.update_dev();

	m_R1.set_R(std::max(m_R() * v, netlist().gmin()));
}

// ----------------------------------------------------------------------------------------
// nld_C
// ----------------------------------------------------------------------------------------

NETLIB_RESET(C)
{
	// FIXME: Startup conditions
	set(netlist().gmin(), 0.0, -5.0 / netlist().gmin());
	//set(netlist().gmin(), 0.0, 0.0);
}

NETLIB_UPDATE_PARAM(C)
{
	m_GParallel = netlist().gmin();
}

NETLIB_UPDATE(C)
{
	NETLIB_NAME(twoterm)::update();
}

NETLIB_TIMESTEP(C)
{
	/* Gpar should support convergence */
	const nl_double G = m_C() / step +  m_GParallel;
	const nl_double I = -G * deltaV();
	set(G, 0.0, I);
}

// ----------------------------------------------------------------------------------------
// nld_L
// ----------------------------------------------------------------------------------------

NETLIB_RESET(L)
{
	set(netlist().gmin(), 0.0, 5.0 / netlist().gmin());
	//set(1.0/NETLIST_GMIN, 0.0, -5.0 * NETLIST_GMIN);
}

NETLIB_UPDATE_PARAM(L)
{
	m_GParallel = netlist().gmin();
}

NETLIB_UPDATE(L)
{
	NETLIB_NAME(twoterm)::update();
}

NETLIB_TIMESTEP(L)
{
	/* Gpar should support convergence */
	m_I += m_I + m_G * deltaV();
	m_G = step / m_L() + m_GParallel;
	set(m_G, 0.0, m_I);
}

// ----------------------------------------------------------------------------------------
// nld_D
// ----------------------------------------------------------------------------------------

NETLIB_UPDATE_PARAM(D)
{
	nl_double Is = m_model.model_value("IS");
	nl_double n = m_model.model_value("N");

	m_D.set_param(Is, n, netlist().gmin());
}

NETLIB_UPDATE(D)
{
	NETLIB_NAME(twoterm)::update();
}

NETLIB_UPDATE_TERMINALS(D)
{
	m_D.update_diode(deltaV());
	set(m_D.G(), 0.0, m_D.Ieq());
}

// ----------------------------------------------------------------------------------------
// nld_VS
// ----------------------------------------------------------------------------------------

NETLIB_RESET(VS)
{
	NETLIB_NAME(twoterm)::reset();
	this->set(1.0 / m_R(), m_V(), 0.0);
}

NETLIB_UPDATE(VS)
{
	NETLIB_NAME(twoterm)::update();
}

// ----------------------------------------------------------------------------------------
// nld_CS
// ----------------------------------------------------------------------------------------

NETLIB_RESET(CS)
{
	NETLIB_NAME(twoterm)::reset();
	this->set(0.0, 0.0, m_I());
}

NETLIB_UPDATE(CS)
{
	NETLIB_NAME(twoterm)::update();
}

	} //namespace devices
} // namespace netlist
