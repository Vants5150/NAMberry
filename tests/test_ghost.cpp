// test_ghost.cpp - PC test of the uninterrupted preset change: ghost (tails), fades, NAM slots, adoption of the document
// g++ -std=c++17 -O1 -Ihost_shim -I<circle>/include -I. -Iadel test_ghost.cpp board.cpp graph.cpp fx*.cpp midi.cpp -o test_ghost
#include <cstdio>
#include <cmath>
#include <cstring>
#include "board.h"
#include "graph.h"
#include "fx.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// test effect: adds to the input a state that decays by 0.999 per sample and recharges with the input (a tail without depending on the real effects)
class CTailFx : public CFx
{
public:
	CTailFx (void) : m_f (0.0f) {}
	void Process (float *pL, float *pR, unsigned n, int *pMono, const float *pP) override
	{
		(void) pP;
		for (unsigned j = 0; j < n; j++)
		{
			m_f = m_f * 0.999f + pL[j];
			pL[j] = m_f;
			pR[j] = m_f;
		}
		*pMono = 1;
	}
	unsigned Type (void) const override { return FxFirst; }
	float m_f;
};

static void MakePlan (TPlan *p, CFx *pFx)
{
	memset (p, 0, sizeof *p);
	p->Node[0].Kind = FxFirst;
	p->Node[0].On = 1;
	p->Node[0].pFx = pFx;
	p->NOrder = 1;
	p->Order[0] = 0;
	p->Link[0].From = PL_NET_IN;
	p->Link[0].PF = 0;
	p->Link[0].PT = 0;
	p->Link[0].Gain = 1.0f;
	p->Link[1].From = 0;
	p->Link[1].PF = 0;
	p->Link[1].PT = 0;
	p->Link[1].Gain = 1.0f;
	for (unsigned i = 0; i <= PL_MAX_NODES; i++)
	{
		p->LFirst[i] = i == 0 ? 0 : 1;
	}
	p->LFirst[PL_MAX_NODES] = 1;
	p->LFirst[PL_MAX_NODES + 1] = 2;
	p->Master = 1.0f;
}

int main ()
{
	static TPlan Old;
	CTailFx *pFx = new CTailFx;
	MakePlan (&Old, pFx);

	// 1. sound with the old plan: an impulse, then silence; the tail is there
	float inL[64], inR[64], oL[64], oR[64];
	GraphResetSmoothing ();
	memset (inL, 0, sizeof inL);
	memset (inR, 0, sizeof inR);
	inL[0] = 1.0f;
	GraphProcess (&Old, inL, inR, oL, oR, 64);
	CHECK (oL[63] > 0.9f);

	// 2. ghost: copy of the plan with the same instance; with zero input the tail continues without jumps and then decays
	TGhost *pG = new TGhost;
	memset (pG, 0, sizeof *pG);
	memcpy (&pG->Plan, &Old, sizeof Old);
	pG->Fx[0] = pFx;
	GraphGhostInit (pG);
	CHECK (pG->State == GH_RUN);
	CHECK (pG->NamLive == 1);
	float fRef = pFx->m_f;		// state the instance would have after the already processed block
	memset (inL, 0, sizeof inL);
	GraphGhostProcess (pG, inL, inR, oL, oR, 64);
	float fExp = fRef;
	for (int j = 0; j < 64; j++)
	{
		fExp = fExp * 0.999f;
	}
	CHECK (fabsf (oL[63] - fExp) < 1e-4f);
	CHECK (fabsf (oL[0] - fRef * 0.999f) < 1e-4f);		// no jump between the old plan and the ghost
	CHECK (pG->NamLive == 1);					// the fade lasts 192 frames: the NAMs are still running
	GraphGhostProcess (pG, inL, inR, oL, oR, 64);
	GraphGhostProcess (pG, inL, inR, oL, oR, 64);
	CHECK (pG->NamLive == 0);					// after the fade the NAMs are excluded

	// 3. the tail dies out by itself: beyond 2 s of silence below -120 dBFS the ghost ends
	unsigned nBlocks = 0;
	while (pG->State == GH_RUN && nBlocks < 4000)
	{
		GraphGhostProcess (pG, inL, inR, oL, oR, 64);
		nBlocks++;
	}
	CHECK (pG->State == GH_DONE);
	CHECK (nBlocks * 64 >= GH_QUIET_FRAMES);
	CHECK (nBlocks * 64 < GH_QUIET_FRAMES + 64000);
	GraphGhostProcess (pG, inL, inR, oL, oR, 64);		// finished: silence
	CHECK (oL[10] == 0.0f);
	delete pFx;
	delete pG;

	// 4. input fade: a ghost with constant input sees it fall from 1 to 0 in GH_RAMP_FRAMES and no further
	CTailFx *pFx2 = new CTailFx;
	MakePlan (&Old, pFx2);
	pG = new TGhost;
	memset (pG, 0, sizeof *pG);
	memcpy (&pG->Plan, &Old, sizeof Old);
	pG->Fx[0] = pFx2;
	GraphGhostInit (pG);
	for (int j = 0; j < 64; j++)
	{
		inL[j] = 1.0f;
		inR[j] = 1.0f;
	}
	double fSum = 0.0;
	for (unsigned f = 0; f < GH_RAMP_FRAMES + 64; f += 64)
	{
		GraphGhostProcess (pG, inL, inR, oL, oR, 64);
	}
	CHECK (pG->RampLeft == 0);
	(void) fSum;
	// 5. replaced by a more recent ghost: it fades in GH_KILL_FRAMES and ends
	pG->Kill = 1;
	pG->KillLeft = GH_KILL_FRAMES;
	memset (inL, 0, sizeof inL);
	unsigned nK = 0;
	float fLast = 1e9f;
	boolean bMono = TRUE;
	while (pG->State == GH_RUN && nK < 20)
	{
		GraphGhostProcess (pG, inL, inR, oL, oR, 64);
		nK++;
		float a = fabsf (oL[0]);
		if (a > fLast + 1e-3f)
		{
			bMono = FALSE;
		}
		fLast = fabsf (oL[63]);
	}
	CHECK (pG->State == GH_DONE);
	CHECK (bMono);
	CHECK (nK * 64 >= GH_KILL_FRAMES && nK * 64 <= GH_KILL_FRAMES + 64);
	delete pFx2;
	delete pG;

	// 6. plan in bypass: nothing to keep
	CTailFx *pFx3 = new CTailFx;
	MakePlan (&Old, pFx3);
	Old.Bypass = 1;
	pG = new TGhost;
	memset (pG, 0, sizeof *pG);
	memcpy (&pG->Plan, &Old, sizeof Old);
	GraphGhostInit (pG);
	GraphGhostProcess (pG, inL, inR, oL, oR, 64);
	CHECK (pG->State == GH_DONE);
	delete pFx3;
	delete pG;

	// 7. document: NAM slots of the new preset in the free slots
	{
		CBoard Live, Stage;
		int a, b, c;
		CHECK (Live.NetAdd ("nam", 1, 3, "a.adn", &a) == 0);	// slot 0
		CHECK (Live.NetAdd ("nam", 3, 3, "b.adn", &b) == 0);	// slot 1
		CHECK (Stage.NetAdd ("nam", 1, 3, "c.adn", &c) == 0);	// slot 0 in its document
		int d;
		CHECK (Stage.NetAdd ("nam", 3, 3, "d.adn", &d) == 0);
		boolean Busy[BD_MAX_NAM];
		for (unsigned s = 0; s < BD_MAX_NAM; s++)
		{
			Busy[s] = FALSE;
		}
		for (unsigned id = 0; id < BD_MAX_BLOCKS; id++)
		{
			if (Live.Block (id).Used && Live.Block (id).Type == NodeNam)
			{
				Busy[Live.Block (id).Slot] = TRUE;
			}
		}
		CHECK (Stage.RemapNamSlots (Busy));
		CHECK (Stage.Block (c).Slot == 2);
		CHECK (Stage.Block (d).Slot == 3);
		// the remapped document is saved and re-read with the same slots
		static char Text[32768];
		Stage.Serialize (Text, sizeof Text);
		CBoard Re;
		CHECK (Re.Parse (Text));
		CHECK (Re.Block (c).Slot == 2 && Re.Block (d).Slot == 3);
		// adoption: the counters continue from those of the document in use, the document in use is the new one
		const unsigned nV = Live.GetVersion (), nS = Live.GetStructVersion ();
		Live.Adopt (Stage);
		CHECK (Live.GetVersion () > nV);
		CHECK (Live.GetStructVersion () > nS);
		CHECK (Live.Block (c).Slot == 2 && strcmp (Live.Block (c).File, "c.adn") == 0);
		CHECK (Live.TakeDirty ());
		CHECK (!Live.TakeSaveDirty ());
		// not enough free slots: nothing changes
		CBoard Many;
		int id;
		for (int r = 0; r < 3; r++)
		{
			CHECK (Many.NetAdd ("nam", r, 2, "x.adn", &id) == 0);
		}
		for (unsigned s = 0; s < BD_MAX_NAM; s++)
		{
			Busy[s] = s < BD_MAX_NAM - 2;
		}
		const int nSlot0 = Many.Block (0).Slot;
		CHECK (!Many.RemapNamSlots (Busy));
		CHECK (Many.Block (0).Slot == nSlot0);
	}

	printf ("RESULT: %s (%d failed)\n", fails == 0 ? "OK" : "FAILED", fails);
	return fails != 0;
}
