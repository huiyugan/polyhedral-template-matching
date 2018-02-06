#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <cmath>
#include <cfloat>
#include <cassert>
#include <algorithm>
#include "convex_hull_incremental.hpp"
#include "canonical_coloured.hpp"
#include "graph_data.hpp"
#include "graph_tools.hpp"
#include "normalize_vertices.hpp"
#include "qcprot/polar.hpp"
#include "initialize_data.hpp"
#include "structure_matcher.hpp"
#include "ptm_constants.h"


static double calc_rmsd(int num_points, const double (*ideal_points)[3], double (*normalized)[3], int8_t* mapping,
			double G1, double G2, double E0, double* q, double* p_scale)
{
	double A0[9];
	InnerProduct(A0, num_points, ideal_points, normalized, mapping);

	double nrmsdsq, rot[9];
	FastCalcRMSDAndRotation(A0, E0, &nrmsdsq, q, rot);

	double k0 = 0;
	for (int i=0;i<num_points;i++)
	{
		for (int j=0;j<3;j++)
		{
			double v = 0.0;
			for (int k=0;k<3;k++)
				v += rot[j*3+k] * ideal_points[i][k];

			k0 += v * normalized[mapping[i]][j];
		}
	}

	double scale = k0 / G2;
	*p_scale = scale;
	return sqrt(fabs(G1 - scale*k0) / num_points);
}

static void check_graphs(	const refdata_t* s,
				uint64_t hash,
				std::array<int8_t, 2 * PTM_MAX_EDGES> &code,
				int8_t* canonical_labelling,
				double (*normalized)[3],
				result_t* res)
{
	//if (s->graphmap->find(code) == s->graphmap->end())
	//	return;

	int num_points = s->num_nbrs + 1;
	const double (*ideal_points)[3] = s->points;
	int8_t inverse_labelling[PTM_MAX_POINTS];
	int8_t mapping[PTM_MAX_POINTS];

	for (int i=0; i<num_points; i++)
		inverse_labelling[ canonical_labelling[i] ] = i;

	double G1 = 0, G2 = 0;
	for (int i=0;i<num_points;i++)
	{
		double x1 = ideal_points[i][0];
		double y1 = ideal_points[i][1];
		double z1 = ideal_points[i][2];

		double x2 = normalized[i][0];
		double y2 = normalized[i][1];
		double z2 = normalized[i][2];

		G1 += x1 * x1 + y1 * y1 + z1 * z1;
		G2 += x2 * x2 + y2 * y2 + z2 * z2;
	}
	double E0 = (G1 + G2) / 2;

	for (int i = 0;i<s->num_graphs;i++)
	{
		if (hash != s->graphs[i].hash)
			continue;

		graph_t* gref = &s->graphs[i];
		for (int j = 0;j<gref->num_automorphisms;j++)
		{
			for (int k=0;k<num_points;k++)
				mapping[automorphisms[gref->automorphism_index + j][k]] = inverse_labelling[ gref->canonical_labelling[k] ];

			double q[4], scale = 0;
			double rmsd = calc_rmsd(num_points, ideal_points, normalized, mapping, G1, G2, E0, q, &scale);
			if (rmsd < res->rmsd)
			{
				res->rmsd = rmsd;
				res->scale = scale;
				res->ref_struct = s;
				memcpy(res->q, q, 4 * sizeof(double));
				memcpy(res->mapping, mapping, sizeof(int8_t) * num_points);
			}
		}
	}

/*
	for ( auto _gref = (*s->graphmap)[code].begin(); _gref != (*s->graphmap)[code].end(); _gref++ )
	{
		graph_t* gref = *_gref;
		assert (hash == gref->hash);

		for (int j = 0;j<gref->num_automorphisms;j++)
		{
			for (int k=0;k<num_points;k++)
				mapping[automorphisms[gref->automorphism_index + j][k]] = inverse_labelling[ gref->canonical_labelling[k] ];

			double q[4], scale = 0;
			double rmsd = calc_rmsd(num_points, ideal_points, normalized, mapping, G1, G2, E0, q, &scale);
			if (rmsd < res->rmsd)
			{
				res->rmsd = rmsd;
				res->scale = scale;
				res->ref_struct = s;
				memcpy(res->q, q, 4 * sizeof(double));
				memcpy(res->mapping, mapping, sizeof(int8_t) * num_points);
			}
		}
	}*/
}

int match_general(const refdata_t* s, double (*ch_points)[3], double (*points)[3], convexhull_t* ch, result_t* res)
{
	int8_t degree[PTM_MAX_NBRS];
	int8_t facets[PTM_MAX_FACETS][3];

	int ret = get_convex_hull(s->num_nbrs + 1, (const double (*)[3])ch_points, ch, facets);
	ch->ok = ret == 0;
	if (ret != 0)
		return PTM_NO_ERROR;

	if (ch->num_facets != s->num_facets)
		return PTM_NO_ERROR;			//incorrect number of facets in convex hull

	int max_degree = graph_degree(s->num_facets, facets, s->num_nbrs, degree);
	if (max_degree > s->max_degree)
		return PTM_NO_ERROR;

	if (s->type == PTM_MATCH_SC)
		for (int i = 0;i<s->num_nbrs;i++)
			if (degree[i] != 4)
				return PTM_NO_ERROR;

	double normalized[PTM_MAX_POINTS][3];
	subtract_barycentre(s->num_nbrs + 1, points, normalized);

	std::array<int8_t, 2 * PTM_MAX_EDGES> code;
	int8_t colours[PTM_MAX_POINTS] = {0};
	int8_t canonical_labelling[PTM_MAX_POINTS];
	uint64_t hash = 0;
	ret = canonical_form_coloured(s->num_facets, facets, s->num_nbrs, degree, colours, canonical_labelling, &code[0], &hash);
	//ret = canonical_form(s->num_facets, facets, s->num_nbrs, degree, canonical_labelling, &hash);
	if (ret != PTM_NO_ERROR)
		return ret;

	check_graphs(s, hash, code, canonical_labelling, normalized, res);
	return PTM_NO_ERROR;
}

int match_fcc_hcp_ico(double (*ch_points)[3], double (*points)[3], int32_t flags, convexhull_t* ch, result_t* res)
{
	int num_nbrs = structure_fcc.num_nbrs;
	int num_facets = structure_fcc.num_facets;
	int max_degree = structure_fcc.max_degree;

	int8_t degree[PTM_MAX_NBRS];
	int8_t facets[PTM_MAX_FACETS][3];

	int ret = get_convex_hull(num_nbrs + 1, (const double (*)[3])ch_points, ch, facets);
	ch->ok = ret == 0;
	if (ret != 0)
		return PTM_NO_ERROR;

	if (ch->num_facets != num_facets)
		return PTM_NO_ERROR;			//incorrect number of facets in convex hull

	int _max_degree = graph_degree(num_facets, facets, num_nbrs, degree);
	if (_max_degree > max_degree)
		return PTM_NO_ERROR;

	double normalized[PTM_MAX_POINTS][3];
	subtract_barycentre(num_nbrs + 1, points, normalized);

	std::array<int8_t, 2 * PTM_MAX_EDGES> code;
	int8_t colours[PTM_MAX_POINTS] = {0};
	int8_t canonical_labelling[PTM_MAX_POINTS];
	uint64_t hash = 0;
	ret = canonical_form_coloured(num_facets, facets, num_nbrs, degree, colours, canonical_labelling, &code[0], &hash);
	//ret = canonical_form(num_facets, facets, num_nbrs, degree, canonical_labelling, &hash);
	if (ret != PTM_NO_ERROR)
		return ret;

	if (flags & PTM_CHECK_FCC)	check_graphs(&structure_fcc, hash, code, canonical_labelling, normalized, res);
	if (flags & PTM_CHECK_HCP)	check_graphs(&structure_hcp, hash, code, canonical_labelling, normalized, res);
	if (flags & PTM_CHECK_ICO)	check_graphs(&structure_ico, hash, code, canonical_labelling, normalized, res);
	return PTM_NO_ERROR;
}

#if 0
int match_dcub_dhex(double (*ch_points)[3], double (*points)[3], int32_t flags, convexhull_t* ch, result_t* res)
{
	int num_nbrs = structure_dcub.num_nbrs;
	int num_facets = structure_dcub.num_facets;
	int max_degree = structure_dcub.max_degree;

	int8_t degree[PTM_MAX_NBRS];
	int8_t facets[PTM_MAX_FACETS][3];


	int ret = get_convex_hull(num_nbrs + 1, (const double (*)[3])ch_points, ch, facets);
	ch->ok = ret == 0;
	if (ret != 0)
		return PTM_NO_ERROR;

	if (ch->num_facets < structure_fcc.num_facets)
		return PTM_NO_ERROR;			//incorrect number of facets in convex hull

for (int i=0;i<ch->num_facets;i++)
{
	for (int j=0;j<3;j++)
	{
		int a = facets[i][j];
		int b = facets[i][(j+1)%3];
		int c = facets[i][(j+2)%3];

		if (a >= 1 && a <= 4)
		{
			a--;
			b--;
			c--;

			b = (b - 4) / 3;
			c = (c - 4) / 3;
			if (b != a || c != a)
			{
				printf("@ %d %d %d\n", facets[i][(j+0)%3], facets[i][(j+1)%3], facets[i][(j+2)%3]);
				return PTM_NO_ERROR;
			}
		}
	}
	int a = facets[i][0];
	int b = facets[i][1];
	int c = facets[i][2];

	if (a >= 1 && a <= 4)	return PTM_NO_ERROR;
	if (b >= 1 && b <= 4)	return PTM_NO_ERROR;
	if (c >= 1 && c <= 4)	return PTM_NO_ERROR;
}

	

	//int _max_degree = graph_degree(num_facets, facets, num_nbrs, degree);
	int _max_degree = graph_degree(ch->num_facets, facets, num_nbrs, degree);
	if (_max_degree > max_degree)
		return PTM_NO_ERROR;

	//sort out convex hull here

	double normalized[PTM_MAX_POINTS][3];
	subtract_barycentre(num_nbrs + 1, points, normalized);

/*
	for (int i=0;i<ch->num_facets;i++)
	{
		int a = facets[i][0];
		int b = facets[i][1];
		int c = facets[i][2];

		if (a >= 0 && a <= 4)	return PTM_NO_ERROR;
		if (b >= 0 && b <= 4)	return PTM_NO_ERROR;
		if (c >= 0 && c <= 4)	return PTM_NO_ERROR;
	}
*/

res->ref_struct = &structure_dcub;
return PTM_NO_ERROR;

	std::array<int8_t, 2 * PTM_MAX_EDGES> code;
	int8_t colours[PTM_MAX_POINTS] = {0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int8_t canonical_labelling[PTM_MAX_POINTS];
	uint64_t hash = 0;
	ret = canonical_form_coloured(num_facets, facets, num_nbrs, degree, colours, canonical_labelling, &code[0], &hash);
	if (ret != PTM_NO_ERROR)
		return ret;

	//if (flags & PTM_CHECK_DCUB)	check_graphs(&structure_dcub, hash, code, canonical_labelling, normalized, res);
	//if (flags & PTM_CHECK_DHEX)	check_graphs(&structure_dhex, hash, code, canonical_labelling, normalized, res);
	return PTM_NO_ERROR;
}
#else

int match_dcub_dhex(double (*_ch_points)[3], double (*_points)[3], int32_t flags, convexhull_t* ch, result_t* res)
{
	int num_nbrs = structure_diac.num_nbrs;
	int num_facets = structure_diac.num_facets;
	int max_degree = structure_diac.max_degree;

double ch_points[PTM_MAX_POINTS][3];
memcpy(&ch_points[0], &_ch_points[0], 3 * sizeof(double));
memcpy(&ch_points[1], &_ch_points[5], 12 * 3 * sizeof(double));

double points[PTM_MAX_POINTS][3];
memcpy(&points[0], &_points[0], 3 * sizeof(double));
memcpy(&points[1], &_points[5], 12 * 3 * sizeof(double));

	int8_t degree[PTM_MAX_NBRS];
	int8_t facets[PTM_MAX_FACETS][3];

	int ret = get_convex_hull(num_nbrs + 1, (const double (*)[3])ch_points, ch, facets);
	ch->ok = ret == 0;
	if (ret != 0)
		return PTM_NO_ERROR;

	if (ch->num_facets != num_facets)
		return PTM_NO_ERROR;			//incorrect number of facets in convex hull

	int _max_degree = graph_degree(num_facets, facets, num_nbrs, degree);
	if (_max_degree > max_degree)
		return PTM_NO_ERROR;



	int _num_nbrs = structure_dcub.num_nbrs;
	int _num_facets = structure_fcc.num_facets;
	max_degree = structure_fcc.max_degree;

ch->ok = false;
	int8_t _facets[PTM_MAX_FACETS][3];
	ret = get_convex_hull(_num_nbrs + 1, (const double (*)[3])_ch_points, ch, _facets);
	ch->ok = ret == 0;
	if (ret != 0)
		return PTM_NO_ERROR;

	if (ch->num_facets != _num_facets)
		return PTM_NO_ERROR;			//incorrect number of facets in convex hull

	int8_t _degree[PTM_MAX_NBRS];
	_max_degree = graph_degree(_num_facets, _facets, _num_nbrs, _degree);
	if (_max_degree > max_degree)
		return PTM_NO_ERROR;

int8_t toadd[4][3];
int num_found = 0;
	for (int i=0;i<ch->num_facets;i++)
	{
		int a = _facets[i][0];
		int b = _facets[i][1];
		int c = _facets[i][2];

		if (a <= 3 || b <= 3 || c <= 3)
			return PTM_NO_ERROR;

		int i0 = (a - 4) / 3;
		int i1 = (b - 4) / 3;
		int i2 = (c - 4) / 3;

		if (i0 == i1 && i0 == i2)
		{
			if (num_found >= 4)
				return PTM_NO_ERROR;

			toadd[num_found][0] = a;
			toadd[num_found][1] = b;
			toadd[num_found][2] = c;
			num_found++;

			memcpy(&_facets[i], &_facets[ch->num_facets - 1], 3 * sizeof(int8_t));
			ch->num_facets--;
			i--;
		}
	}

	if (num_found != 4)
		return PTM_NO_ERROR;

	for (int i=0;i<4;i++)
	{
		int a = toadd[i][0];
		int b = toadd[i][1];
		int c = toadd[i][2];

		int i0 = (a - 4) / 3;

		//add_facet(_points, i0, a, b, _facets[ch->num_facets], ch->plane_normal[ch->num_facets], ch->barycentre);
		_facets[ch->num_facets][0] = i0;
		_facets[ch->num_facets][1] = b;
		_facets[ch->num_facets][2] = c;
		ch->num_facets++;

		//add_facet(_points, i0, b, c, _facets[ch->num_facets], ch->plane_normal[ch->num_facets], ch->barycentre);
		_facets[ch->num_facets][0] = a;
		_facets[ch->num_facets][1] = i0;
		_facets[ch->num_facets][2] = c;
		ch->num_facets++;

		//add_facet(_points, i0, c, a, _facets[ch->num_facets], ch->plane_normal[ch->num_facets], ch->barycentre);
		_facets[ch->num_facets][0] = a;
		_facets[ch->num_facets][1] = b;
		_facets[ch->num_facets][2] = i0;
		ch->num_facets++;
	}

	{
		double normalized[PTM_MAX_POINTS][3];
		subtract_barycentre(_num_nbrs + 1, _points, normalized);

		std::array<int8_t, 2 * PTM_MAX_EDGES> code;
		int8_t colours[PTM_MAX_POINTS] = {0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		int8_t canonical_labelling[PTM_MAX_POINTS];
		uint64_t hash = 0;
		ret = canonical_form_coloured(ch->num_facets, _facets, _num_nbrs, _degree, colours, canonical_labelling, &code[0], &hash);
		if (ret != PTM_NO_ERROR)
			return ret;
	}

	double normalized[PTM_MAX_POINTS][3];
	subtract_barycentre(num_nbrs + 1, points, normalized);

	std::array<int8_t, 2 * PTM_MAX_EDGES> code;
	int8_t colours[PTM_MAX_POINTS] = {0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int8_t canonical_labelling[PTM_MAX_POINTS];
	uint64_t hash = 0;
	ret = canonical_form_coloured(num_facets, facets, num_nbrs, degree, colours, canonical_labelling, &code[0], &hash);
	if (ret != PTM_NO_ERROR)
		return ret;

	if (flags & PTM_CHECK_DCUB)	check_graphs(&structure_diac, hash, code, canonical_labelling, normalized, res);
	if (flags & PTM_CHECK_DHEX)	check_graphs(&structure_diah, hash, code, canonical_labelling, normalized, res);


/*if (res->rmsd < 1.05 && res->ref_struct == &structure_diac)
{
printf("%f \n", res->rmsd);
	//printf("@type: %d\n", type);
	printf("@rmsd: %f\n", res->rmsd);
	for (int j=0;j<num_nbrs+1+4;j++)
	{
		printf("!!!%f %f %f\n", _points[j][0], _points[j][1], _points[j][2]);
	}
}*/

	return PTM_NO_ERROR;
}

#endif

