#include <array>
#include <cmath>

#include <GEL/CGLA/CGLA.h>
#include <GEL/Geometry/KDTree.h>
#include <GEL/Geometry/Graph.h>
#include <GEL/HMesh/HMesh.h>
#include <GEL/Geometry/GridAlgorithm.h>
#include <GEL/Geometry/graph_io.h>
#include <GEL/Geometry/graph_util.h>
#include <GEL/Geometry/SphereDelaunay.h>

using namespace Geometry;
using namespace CGLA;
using namespace HMesh;
using namespace std;
using NodeID = AMGraph::NodeID;
using NodeSet = AMGraph::NodeSet;

// Initialize global arrays

map<NodeID, int> val2deg;
map<pair<NodeID,NodeID>, int> branchdeg;
map<pair<NodeID,NodeID>, HMesh::FaceID> branchface;
map<pair<NodeID,NodeID>, HMesh::FaceID> branch_best_face;
map<pair<NodeID,NodeID>, HMesh::VertexID> branch_best_vertex;
map<pair<NodeID,NodeID>, HMesh::VertexID> one_ring_vertex;
map<pair<NodeID,NodeID>, CGLA::Vec3d> branch2vert;
map<FaceID, VertexID> face_vertex;
map<FaceID, VertexID> one_ring_face_vertex;
map<FaceID, int> val2_faces;

void clear_global_arrays() {
  val2deg.clear();
  branchdeg.clear();
  branchface.clear();
  branch_best_face.clear();
  branch_best_vertex.clear();
  branch2vert.clear();
  face_vertex.clear();
  one_ring_face_vertex.clear();
  return;
}

//Graph util functions

vector<NodeID> next_neighbours(Geometry::AMGraph3D& g, NodeID prev, NodeID curr) {

    vector<NodeID> neighbour_list;
    auto N = g.neighbors(curr);
    for (auto next: N) {
        if(next != prev)
            neighbour_list.push_back(next);
    }
    return neighbour_list;

}

NodeID next_jn(Geometry::AMGraph3D& g, NodeID n, NodeID nn) {
  auto N = next_neighbours(g, n, nn);
  NodeID curr_node = nn;
  NodeID prev_node = n;

  while(true) {
      auto curr_nbs = next_neighbours(g, prev_node, curr_node);
      if(curr_nbs.size() != 1) {
          return curr_node;
      }
      prev_node = curr_node;
      curr_node = curr_nbs[0];
  }

}

//Mesh util functions

void id_preserving_cc(HMesh::Manifold& m_in) {

    vector<FaceID> base_faces;
    int Invalid = -1;
    HalfEdgeAttributeVector<int> htouched(m_in.allocated_halfedges(), Invalid);
    int Valid = 1;
    map<FaceID,VertexID> face2centerv;
    vector<HalfEdgeID> base_edges;

    vector<HalfEdgeID> new_edges;


    for(auto f: m_in.faces())
        base_faces.push_back(f);

    for(auto h: m_in.halfedges())
        base_edges.push_back(h);

    for(auto f: base_faces)
        if(m_in.in_use(f)) {
            VertexID center_v = m_in.split_face_by_vertex(f);
            for(Walker w = m_in.walker(center_v); !w.full_circle(); w = w.circulate_vertex_ccw()) {
                new_edges.push_back(w.halfedge());
                face2centerv.insert(std::make_pair(w.face(), center_v));
            }
        }

    FaceAttributeVector<int> ftouched(m_in.allocated_faces(), Invalid);

    for (auto h: base_edges) {
        FaceID f1 = m_in.walker(h).face();
        FaceID f2 = m_in.walker(h).opp().face();
        if(ftouched[f1]==Invalid && ftouched[f2]==Invalid) {
            VertexID opp_v = m_in.split_edge(h);
            ftouched[f1] = Valid;
            ftouched[f2] = Valid;
            m_in.split_face_by_edge(f1, face2centerv.find(f1)->second, opp_v);
            m_in.split_face_by_edge(f2, face2centerv.find(f2)->second, opp_v);
         }

    }
    for (auto h_dissolve: new_edges)
        if(m_in.in_use(h_dissolve))
            m_in.merge_faces(m_in.walker(h_dissolve).face(), h_dissolve);
    return;

}

void quad_mesh_leaves(HMesh::Manifold& m) {

    vector<FaceID> base_faces;
    vector<HalfEdgeID> new_edges;

    int dissolve_flag = 0;


    for(auto f: m.faces())
        if(no_edges(m, f) != 4  || (val2_faces.count(f) && one_ring_face_vertex.count(f))) {
             HalfEdgeID ref_h;

             VertexID ref_v  = one_ring_face_vertex[f];

             if(ref_v == InvalidVertexID)
                 continue;

            VertexID center_v = m.split_face_by_vertex(f);
            int counter = 0;
            int dissolve_flag = 0;
            for(Walker w = m.walker(center_v); !w.full_circle(); w = w.circulate_vertex_ccw()) {
                if(m.walker(w.halfedge()).vertex() == ref_v || m.walker(w.halfedge()).opp().vertex() == ref_v) {
                    dissolve_flag = counter%2;
                    ref_h = w.halfedge();
                }
                counter++;
            }
            counter = 0;
            for(Walker w = m.walker(center_v); !w.full_circle(); w = w.circulate_vertex_ccw()) {
                if(counter%2 != dissolve_flag)
                    new_edges.push_back(w.halfedge());
                counter++;
            }

        }


    for (auto h_dissolve: new_edges)
        if(m.in_use(h_dissolve))
            m.merge_faces(m.walker(h_dissolve).face(), h_dissolve);


    return;


}

VertexID split_LIE(Manifold& mani, HalfEdgeID h) {
    Walker w = mani.walker(h);
    VertexID v = w.next().vertex();
    VertexID vo = w.opp().next().vertex();
    FaceID f = w.face();
    FaceID fo = w.opp().face();

    if(v == vo) {
        return InvalidVertexID;
    }
    VertexID vid = mani.split_edge(h);

    mani.split_face_by_edge(f, vid, v);
    mani.split_face_by_edge(fo, vid, vo);
    return vid;
}

bool check_planar(HMesh::Manifold &m, HalfEdgeID h) {

    FaceID face_1 = m.walker(h).face();
    FaceID face_2 = m.walker(h).opp().face();

    Vec3d normal_1 = normal(m, face_1);

    Vec3d normal_2 = normal(m, face_2);

    Vec3d center_1 = centre(m, face_1);

    Vec3d center_2 = centre(m, face_2);

    float dot_val_1 = dot(normal_1, center_2 - center_1);

    float dot_val_2 = dot(normal_2, center_1 - center_2);

    if(dot_val_1 > 0 && dot_val_2 > 0)
        return true;


    float dot_val = dot(normal_1 , normal_2);

    if(dot_val < 0)
        return false;

    dot_val = abs(dot_val);

    if(dot_val < 0.75)
        return false;
    else
        return true;
}

bool check_convex(HMesh::Manifold &m, HalfEdgeID h) {

    auto v1 = m.walker(h).vertex();
    auto v2 = m.walker(h).opp().vertex();

    auto v3 = m.walker(h).next().vertex();
    auto v4 = m.walker(h).opp().prev().prev().vertex();

    Vec3d edge_vec_1 = m.pos(v3) - m.pos(v1);
    Vec3d edge_vec_2 = m.pos(v1) - m.pos(v4);

    Vec3d edge_vec_3 = m.pos(v3) - m.pos(v2);
    Vec3d edge_vec_4 = m.pos(v2) - m.pos(v4);

    Vec3d diag_vec = m.pos(v2) - m.pos(v1);

    float angle_1 = acos(dot(normalize(diag_vec),normalize(-edge_vec_3))) + acos(dot(normalize(diag_vec),normalize(edge_vec_4)));

    float angle_2 = acos(dot(normalize(diag_vec),normalize(edge_vec_1))) + acos(dot(normalize(diag_vec),normalize(-edge_vec_2)));

    angle_1 *= 180/3.14;
    angle_2 *= 180/3.14;

    if (angle_1 > 180 || angle_2 > 180)
        return false;
    else
        return true;
}

void stellate_face_set_retopo(HMesh::Manifold &m, HMesh::FaceSet fs, HMesh::HalfEdgeSet hs) {

  HalfEdgeSet interior_edges;

  HalfEdgeSet visited;

  VertexSet aux_vertices;

  for (auto h : hs) {
    if(visited.find(h) != visited.end() || visited.find(m.walker(h).opp().halfedge()) != visited.end())
      continue;

    VertexID interior_vertex = m.walker(h).next().vertex();
    VertexID interior_vertex_opp = m.walker(h).prev().prev().vertex();

    if(aux_vertices.find(interior_vertex) != aux_vertices.end() || aux_vertices.find(interior_vertex_opp) != aux_vertices.end()) {
      m.flip_edge(h);
      visited.insert(h);
      visited.insert(m.walker(h).opp().halfedge());
      continue;
    }


    visited.insert(h);
    visited.insert(m.walker(h).opp().halfedge());
    VertexID curr_v = split_LIE(m, h);
    aux_vertices.insert(curr_v);
    for (auto h_flip : hs) {
      if(visited.find(h_flip) == visited.end() && (m.walker(h_flip).next().vertex() == curr_v || m.walker(h_flip).prev().prev().vertex() == curr_v) && check_planar(m,h_flip)) {
        m.flip_edge(h_flip);
        visited.insert(h_flip);
        visited.insert(m.walker(h_flip).opp().halfedge());
      }
    }

  }
  for (auto h_flip : m.halfedges()) {
    VertexID interior_vertex = m.walker(h_flip).next().vertex();
    if(visited.find(h_flip) == visited.end() && aux_vertices.find(interior_vertex) != aux_vertices.end() && check_planar(m, h_flip)) {
      m.flip_edge(h_flip);
      visited.insert(h_flip);
      visited.insert(m.walker(h_flip).opp().halfedge());
    }
  }

}

vector<FaceSet> retopologize_planar_regions(HMesh::Manifold &m) {

// find triangles with low dihedral angle between them

    vector<FaceSet> planar_regions;

    vector<HalfEdgeSet> planar_edges;

    // identify planar regions

    FaceSet global_visited;

    for (auto f : m.faces()) {

        if(global_visited.find(f) != global_visited.end())
            continue;

        FaceSet planar_set;
        HalfEdgeSet planar_edge_set;

        FaceSet visited;

        queue<FaceID> Q;

        Q.push(f);

        while(!Q.empty()) {

            FaceID curr_f = Q.front();

            HalfEdgeSet edge_set;


            for(Walker w = m.walker(curr_f); !w.full_circle(); w = w.circulate_face_ccw()) {
                edge_set.insert(w.halfedge());
            }

            for (auto h : edge_set)
                if(check_planar(m,h)) {
                  planar_edge_set.insert(h);
                  planar_edge_set.insert(m.walker(h).opp().halfedge());
                  FaceID f1 = m.walker(h).face();
                  FaceID f2 = m.walker(h).opp().face();

                  planar_set.insert(f1);
                  planar_set.insert(f2);

                  global_visited.insert(f1);
                  global_visited.insert(f2);

                  if(visited.find(f1) == visited.end())
                    Q.push(f1);
                  if(visited.find(f2) == visited.end())
                    Q.push(f2);
                }

            visited.insert(curr_f);
            Q.pop();
       }

       planar_regions.push_back(planar_set);
       planar_edges.push_back(planar_edge_set);
    }
    int id = 0;
    for (auto fset : planar_regions) {
      if(fset.size() > 2)
        stellate_face_set_retopo(m, fset, planar_edges[id]);
      id++;
    }

    return planar_regions;

}

//Graph - Mesh relationship Functions

VertexID branch2vertex (HMesh::Manifold &m_out, Geometry::AMGraph3D& g, NodeID n, NodeID nn, Util::AttribVec<NodeID, FaceSet> node2fs) {

    Vec3d vert_pos = branch2vert.find(std::make_pair(n,nn))->second;

    for (auto v: m_out.vertices())
        if(sqr_length(m_out.pos(v) - vert_pos) == 0)
            return v;

    return InvalidVertexID;

}

void init_branch_degree(HMesh::Manifold &m, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet> node2fs) {


    for (auto n:g.node_ids()) {
        auto N = g.neighbors(n);

        //for all branch nodes

        if(N.size() > 2) {


        // for each outgoing arc

            for (auto nn: N) {

                int src_branch_degree = valency(m, branch2vertex(m, g, n,nn, node2fs));
                vector<NodeID> branch_path;
                NodeID curr_node = nn;
                NodeID prev_node = n;

                int leaf_flag = 0;

              //traverse val 2 nodes to next branch node


                while(true) {
                    auto curr_nbs = next_neighbours(g, prev_node, curr_node);
                    if(curr_nbs.size() > 1)
                        break;
                    else if(curr_nbs.size() == 0) {
                        branch_path.push_back(curr_node); leaf_flag = 1; break;
                    }
                    else {
                        branch_path.push_back(curr_node);
                        prev_node = curr_node;
                        curr_node = curr_nbs[0];
                    }
                }

                //pick lower degree

                int dest_branch_degree;
                if(leaf_flag == 1) {
                    dest_branch_degree = src_branch_degree;
                }
                else
                    dest_branch_degree = valency(m, branch2vertex(m, g, curr_node, prev_node, node2fs));


                int path_degree = 0;
                int jn_degree = 0;

                if(dest_branch_degree < src_branch_degree) {
                    path_degree = (dest_branch_degree)*2;
                    jn_degree = dest_branch_degree - 1;
                }

                else if (dest_branch_degree == src_branch_degree) {
                    path_degree = dest_branch_degree*2;
                    jn_degree = dest_branch_degree;
                }

                else {
                    jn_degree = src_branch_degree - 1;
                    path_degree = (src_branch_degree)*2;
                }

                auto key = std::make_pair(n,nn);
                branchdeg.insert(std::make_pair(key,jn_degree));
                for (auto val2node : branch_path)
                    val2deg.insert(std::make_pair(val2node,path_degree));

            }
        }
    }

    // for junction-less graphs

    bool has_junction = false;

    for (auto n: g.node_ids()) {
      if(g.valence(n) > 2)
        has_junction = true;
    }

    if(!has_junction)
      for (auto n : g.node_ids())
        if(g.valence(n) <= 2)
          if(val2deg.find(n) == val2deg.end())
            val2deg.insert(std::make_pair(n,4));

}

FaceID branch2face (HMesh::Manifold &m_out, Geometry::AMGraph3D& g, NodeID n, NodeID nn, Util::AttribVec<NodeID, FaceSet> node2fs) {

    VertexID v = branch2vertex(m_out, g, n, nn, node2fs);
    vector<FaceID> face_set;

    double d_max = FLT_MAX;
    FaceID f_max = InvalidFaceID;
    Vec3d pn = g.pos[n];
    Vec3d pnn = g.pos[nn];
    Vec3d v_n_nn = pnn - pn;


    for(Walker w = m_out.walker(v); !w.full_circle(); w = w.circulate_vertex_ccw()) {
        face_set.push_back(w.face());
    }

    for(auto f: face_set) {
        double d = dot(v_n_nn, normal(m_out, f));

        Vec3d face_normal = normal(m_out, f);
        Vec3d face_center = centre(m_out,f);
        float face_plane_d = dot(face_normal, face_center);

        float intersection_x = (face_plane_d - dot(face_normal, pn)) / dot(face_normal, v_n_nn);

        Vec3d intersection_pt = pn + intersection_x*v_n_nn;

        d = sqr_length(face_center - intersection_pt);

        if(d < d_max) {
            f_max = f;
            d_max = d;
        }
    }
    if(g.neighbors(n).size()>2)
        node2fs[n].erase(f_max);
    return f_max;


}

double face_dist(HMesh::Manifold &m_out, Geometry::AMGraph3D& g, NodeID n, NodeID nn, FaceID f) {
    Vec3d pn = g.pos[n];
    Vec3d pnn = g.pos[nn];
    Vec3d v_n_nn = pnn - pn;


    if(!m_out.in_use(f))
        return 1;

    Vec3d face_normal = normal(m_out, f);
    Vec3d face_center = centre(m_out,f);
    float face_plane_d = dot(face_normal, face_center);

    float intersection_x = 0;
    if(dot(face_normal, v_n_nn) != 0)
       intersection_x = (face_plane_d - dot(face_normal, pn)) / dot(face_normal, v_n_nn);

    Vec3d intersection_pt = pn + intersection_x*v_n_nn;

    float d = sqr_length(face_center - intersection_pt);

    return d;

}

void init_branch_face_pairs(HMesh::Manifold &m, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet> node2fs) {

    for (auto n:g.node_ids()) {

        auto N = g.neighbors(n);

        //for all branch nodes

        if(N.size() > 2) {

            // for each outgoing arc

            for (auto nn: N) {

                auto key = std::make_pair(n,nn);

                FaceID f = branch2face(m, g, n, nn, node2fs);
                branch_best_face.insert(std::make_pair(key,f));

                VertexID v = branch2vertex(m, g, n, nn, node2fs);
                branch_best_vertex.insert(std::make_pair(key,v));

                one_ring_vertex.insert(std::make_pair(key,InvalidVertexID));

            }
        }

    }
}

//Functions for constructing / editing mesh elements from skeletal nodes

vector<Vec3d> get_face_points(int n) {

    vector<Vec3d> face_vertices;
    double h = 0.5;
    double angle = 0;

    for(int i =0; i<n; i++) {

        Vec3d face_vertex = Vec3d(0, h*cos(angle), h*sin(angle));
        face_vertices.push_back(face_vertex);
        angle+=2*22.0/(7.0*n);
    }
    return face_vertices;
}

vector<FaceID> create_face_pair(Manifold& m, const Vec3d& pos, const Mat3x3d& _R, int axis, int num_sides) {
    double h = 0.5;
    if(num_sides == 0) {
        vector<FaceID>fvec;
        return fvec;
    }

    vector<Vec3d> face_points = get_face_points(num_sides);




    Mat3x3d R = _R;
    double det = determinant(R);
    if(abs(det))
        if(det<0) {
            Mat3x3d M = identity_Mat3x3d();
            M[2][2] = -1;
            R = R * M;
        }

    vector<FaceID> fvec;
        vector<Vec3d> front_pts;
        for(int i = 0; i < num_sides; i++)
        {
            Vec3d _p = face_points[i];
            Vec3d p(0);
            p[(0+axis)%3] += _p[0];
            p[(1+axis)%3] += _p[1];
            p[(2+axis)%3] += _p[2];
            front_pts.push_back(R*p+pos);
        }
        fvec.push_back(m.add_face(front_pts));

        vector<Vec3d> back_pts;
        for (int i = 0; i < num_sides; i++) {
            int curr_index = 1 - i;
            if(curr_index < 0 )
                curr_index = num_sides + curr_index;
            Vec3d _p = face_points[curr_index];
            Vec3d p(0);
            p[(0+axis)%3] += _p[0];
            p[(1+axis)%3] += _p[1];
            p[(2+axis)%3] += _p[2];
            back_pts.push_back(R*p+pos);
        }


        fvec.push_back(m.add_face(back_pts));

    return fvec;
}

void val2nodes_to_boxes(Geometry::AMGraph3D& g, HMesh::Manifold& mani, Util::AttribVec<NodeID, FaceSet>& n2fs, double r) {
    Vec3d c(0);
    for(auto n: g.node_ids())
        c += g.pos[n];
    c /= g.no_nodes();
    double min_dist=DBL_MAX;
    NodeID middle_node = *begin(g.node_ids());
    for(auto n: g.node_ids())
        if(g.valence(n)>2)
        {
            double d = sqr_length(g.pos[n]-c);
            if(d < min_dist) {
                min_dist = d;
                middle_node = n;
            }
        }

    bool has_junction = false;

    for (auto n: g.node_ids()) {
      if(g.valence(n) > 2)
        has_junction = true;
    }

    if(!has_junction)
      if(g.valence(middle_node) == 0)
        for (auto n : g.node_ids())
          if(g.valence(n) > 0)
          {
            double d = sqr_length(g.pos[n] - c);
            if(d < min_dist) {
              min_dist = d;
              middle_node = n;
            }
          }

    Util::AttribVec<NodeID, int> touched(g.no_nodes(),0);
    Util::AttribVec<NodeID, Mat3x3d> warp_frame(g.no_nodes(),identity_Mat3x3d());

    queue<NodeID> Q;
    Q.push(middle_node);

    if(has_junction)
      touched[middle_node] = 1;

    while(!Q.empty()) {
        NodeID n = Q.front();
        Q.pop();
        for(auto m : g.neighbors(n))
            if (!touched[m]) {
                Q.push(m);
                touched[m] = 1;
                Vec3d v = g.pos[m]-g.pos[n];
                Mat3x3d M = warp_frame[n];
                Vec3d warp_v = M * v;

                double max_sgn = sign(warp_v[0]);
                double max_val = abs(warp_v[0]);
                int max_idx = 0;
                for(int i=1;i<3;++i) {
                    if(abs(warp_v[i])>max_val) {
                        max_sgn = sign(warp_v[i]);
                        max_val = abs(warp_v[i]);
                        max_idx = i;
                    }
                }
                auto v_target = max_sgn * normalize(v);
                Quatd q;
                q.make_rot(M[max_idx], v_target);
                M = transpose(q.get_Mat3x3d() * transpose(M));
                warp_frame[m] = M;

                if(g.neighbors(m).size()<=2) {
                    Vec3d s(r);
                    Mat3x3d S = scaling_Mat3x3d(s);
                    auto face_list = create_face_pair(mani, g.pos[m], transpose(M)*S, max_idx, val2deg.find(m)->second);
                    stitch_mesh(mani, 1e-10);
                    for(auto f: face_list) {
                            n2fs[m].insert(f);
                            val2_faces.insert(std::make_pair(f, 1));
                    }
                }


            }
    }

}

void val2nodes_to_boxes_radius(Geometry::AMGraph3D& g, HMesh::Manifold& mani, Util::AttribVec<NodeID, FaceSet>& n2fs, vector<double> r) {
    Vec3d c(0);
    for(auto n: g.node_ids())
        c += g.pos[n];
    c /= g.no_nodes();
    double min_dist=DBL_MAX;
    NodeID middle_node = *begin(g.node_ids());
    for(auto n: g.node_ids())
        if(g.valence(n)>2)
        {
            double d = sqr_length(g.pos[n]-c);
            if(d < min_dist) {
                min_dist = d;
                middle_node = n;
            }
        }
    if(g.valence(middle_node) == 0)
      for (auto n : g.node_ids())
        if(g.valence(n) > 0)
        {
          double d = sqr_length(g.pos[n] - c);
          if(d < min_dist) {
            min_dist = d;
            middle_node = n;
          }
        }
    Util::AttribVec<NodeID, int> touched(g.no_nodes(),0);
    Util::AttribVec<NodeID, Mat3x3d> warp_frame(g.no_nodes(),identity_Mat3x3d());

    queue<NodeID> Q;
    Q.push(middle_node);

    if(g.valence(middle_node) > 2)
      touched[middle_node] = 1;

    while(!Q.empty()) {
        NodeID n = Q.front();
        Q.pop();
        for(auto m : g.neighbors(n))
            if (!touched[m]) {
                Q.push(m);
                touched[m] = 1;
                Vec3d v = g.pos[m]-g.pos[n];
                Mat3x3d M = warp_frame[n];
                Vec3d warp_v = M * v;

                double max_sgn = sign(warp_v[0]);
                double max_val = abs(warp_v[0]);
                int max_idx = 0;
                for(int i=1;i<3;++i) {
                    if(abs(warp_v[i])>max_val) {
                        max_sgn = sign(warp_v[i]);
                        max_val = abs(warp_v[i]);
                        max_idx = i;
                    }
                }
                auto v_target = max_sgn * normalize(v);
                Quatd q;
                q.make_rot(M[max_idx], v_target);
                M = transpose(q.get_Mat3x3d() * transpose(M));
                warp_frame[m] = M;

                if(g.neighbors(m).size()<=2) {
                    Vec3d s(r[m]);
                    Mat3x3d S = scaling_Mat3x3d(s);
                    auto face_list = create_face_pair(mani, g.pos[m], transpose(M)*S, max_idx, val2deg.find(m)->second);
                    stitch_mesh(mani, 1e-10);
                    for(auto f: face_list) {
                            n2fs[m].insert(f);
                            val2_faces.insert(std::make_pair(f, 1));
                    }
                }


            }
    }

}

void construct_bnps(HMesh::Manifold &m_out, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet>& node2fs, double r) {

  map<int, pair<NodeID,NodeID>> spts2branch;
  map <int, VertexID> spts2vertexid;


  for (auto n: g.node_ids()) {

          auto N = g.neighbors(n);
          if(N.size()>2) {
              Manifold m;
              int node_vertex_count =0;
              Vec3d pn = g.pos[n];

              auto project_to_sphere = [&]() {
                  for(int iter=0;iter<3;++iter) {
                      auto new_pos = m.positions_attribute_vector();
                      for(auto v: m.vertices())
                          new_pos[v] = normalize(0.5*normal(m,v) + m.pos(v));
                      m.positions_attribute_vector() = new_pos;
                  }
                  for(auto v: m.vertices())
                      m.pos(v) = normalize(m.pos(v))*r + pn;
              };
              vector<Vec3d> spts;
              int spts_vertex_count = 0;

              spts2branch.clear();
              spts2vertexid.clear();

              for (auto nn: N) {
                  Vec3d pnn = g.pos[nn];
                  spts.push_back(normalize(pnn-pn));

                  auto spts_value = std::make_pair(n,nn);
                  auto spts_key = spts_vertex_count;
                  spts2branch.insert(std::make_pair(spts_key,spts_value));
                  spts_vertex_count++;

              }

              bool ghost_added = false;
              if(spts.size()==3) {
                  Vec3d centroid_ghost_pt(0);

                  Vec3d nb_pt_1 = g.pos[n] + normalize(g.pos[N[0]] - g.pos[n]);
                  Vec3d nb_pt_2 = g.pos[n] + normalize(g.pos[N[1]] - g.pos[n]);
                  Vec3d nb_pt_3 = g.pos[n] + normalize(g.pos[N[2]] - g.pos[n]);

                  if(sqr_length(nb_pt_1 - nb_pt_2) < sqr_length(nb_pt_2 - nb_pt_3)) {
                      if(sqr_length(nb_pt_1 - nb_pt_2) < sqr_length(nb_pt_1 - nb_pt_3))
                          spts.push_back(normalize(0.5*(nb_pt_1 + nb_pt_2) - g.pos[n]));
                      else
                          spts.push_back(normalize(0.5*(nb_pt_1 + nb_pt_3) - g.pos[n]));
                  }
                  else {
                      if(sqr_length(nb_pt_2 - nb_pt_3) < sqr_length(nb_pt_1 - nb_pt_3))
                          spts.push_back(normalize(0.5*(nb_pt_2 + nb_pt_3) - g.pos[n]));
                      else
                          spts.push_back(normalize(0.5*(nb_pt_1 + nb_pt_3) - g.pos[n]));
                  }
                  ghost_added = true;
              }


              std::vector<CGLA::Vec3i> stris = SphereDelaunay(spts);

              for(auto tri: stris) {
                  vector<Vec3d> triangle_pts;
                  for(int i=0;i<3; ++i) {
                      triangle_pts.push_back(spts[tri[i]]);

                      auto key = spts2branch.find(tri[i])->second;
                      auto value = tri[i];

                      node_vertex_count++;

                  }
                  m.add_face(triangle_pts);
              }

              stitch_mesh(m, 1e-10);

              m.cleanup();

              for(auto v: m.vertices())
                  for(int i = 0; i < spts.size(); i++)
                      if(sqr_length(m.pos(v) - spts[i]) < 0.0001)
                          spts2vertexid.insert(std::make_pair(i, v));



              if(N.size() > 3 && !ghost_added)
                  vector<FaceSet> planar_regions = retopologize_planar_regions(m);

              project_to_sphere();

              for(int i = 0; i < spts.size(); i++) {
                  auto key = spts2branch.find(i)->second;
                  auto value = m.pos(spts2vertexid.find(i)->second);
                  branch2vert.insert(std::make_pair(key,value));
              }

              m.cleanup();

              size_t no_faces_before = m_out.no_faces();

              m_out.merge(m);
              for(auto f: m_out.faces())
                  if(f.index >= no_faces_before)
                      node2fs[n].insert(f);
          }
    }
    id_preserving_cc(m_out);
    m_out.cleanup();
    stitch_mesh(m_out, 1e-10);
}

void construct_bnps_radius(HMesh::Manifold &m_out, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet>& node2fs, vector<double> r_arr) {

  map<int, pair<NodeID,NodeID>> spts2branch;
  map <int, VertexID> spts2vertexid;

  double base_r = 0.5 * g.average_edge_length();

  double r = base_r;

  for (auto n: g.node_ids()) {

          auto N = g.neighbors(n);
          if(N.size()>2) {
              Manifold m;
              int node_vertex_count =0;
              Vec3d pn = g.pos[n];

              r = base_r;

              if(r_arr[n] > base_r)
                r = r_arr[n];

              auto project_to_sphere = [&]() {
                  for(int iter=0;iter<3;++iter) {
                      auto new_pos = m.positions_attribute_vector();
                      for(auto v: m.vertices())
                          new_pos[v] = normalize(0.5*normal(m,v) + m.pos(v));
                      m.positions_attribute_vector() = new_pos;
                  }
                  for(auto v: m.vertices())
                      m.pos(v) = normalize(m.pos(v))*r + pn;
              };
              vector<Vec3d> spts;
              int spts_vertex_count = 0;

              spts2branch.clear();
              spts2vertexid.clear();

              for (auto nn: N) {
                  Vec3d pnn = g.pos[nn];
                  spts.push_back(normalize(pnn-pn));

                  auto spts_value = std::make_pair(n,nn);
                  auto spts_key = spts_vertex_count;
                  spts2branch.insert(std::make_pair(spts_key,spts_value));
                  spts_vertex_count++;

              }
              bool ghost_added = false;
              if(spts.size()==3) {
                  Vec3d centroid_ghost_pt(0);
                  for (auto nn : N)
                      centroid_ghost_pt+=g.pos[nn];
                  centroid_ghost_pt/=3;
                  spts.push_back(normalize(0.5*(g.pos[N[0]] + g.pos[N[1]]) - g.pos[n]));
                  spts.push_back(normalize(0.5*(g.pos[N[1]] + g.pos[N[2]]) - g.pos[n]));
                  spts.push_back(normalize(0.5*(g.pos[N[0]] + g.pos[N[2]]) - g.pos[n]));
                  ghost_added = true;

              }


              std::vector<CGLA::Vec3i> stris = SphereDelaunay(spts);

              for(auto tri: stris) {
                  vector<Vec3d> triangle_pts;
                  for(int i=0;i<3; ++i) {
                      triangle_pts.push_back(spts[tri[i]]);
                      auto key = spts2branch.find(tri[i])->second;
                      auto value = tri[i];

                      node_vertex_count++;

                  }
                  m.add_face(triangle_pts);
              }
              stitch_mesh(m, 1e-10);

              m.cleanup();

              for(auto v: m.vertices())
                  for(int i = 0; i < spts.size(); i++)
                      if(sqr_length(m.pos(v) - spts[i]) < 0.0001)
                          spts2vertexid.insert(std::make_pair(i, v));


              if(N.size() > 3 && !ghost_added)
                vector<FaceSet> planar_regions = retopologize_planar_regions(m);

              project_to_sphere();

              for(int i = 0; i < spts.size(); i++) {
                  auto key = spts2branch.find(i)->second;
                  auto value = m.pos(spts2vertexid.find(i)->second);
                  branch2vert.insert(std::make_pair(key,value));
              }

              m.cleanup();

              size_t no_faces_before = m_out.no_faces();



              m_out.merge(m);
              for(auto f: m_out.faces())
                  if(f.index >= no_faces_before)
                      node2fs[n].insert(f);
          }
    }
    id_preserving_cc(m_out);
    m_out.cleanup();
    stitch_mesh(m_out, 1e-10);
}

void merge_branch_faces(HMesh::Manifold &m, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet> node2fs) {

    VertexID v;
    FaceID f, face_1, face_2;
    int branch_degree;
    HalfEdgeID boundary_edge_1;
    HalfEdgeID boundary_edge_2;


    for (auto n:g.node_ids()) {
        auto N = g.neighbors(n);

        //for all branch nodes

        if(N.size() > 2) {

            // for each outgoing arc

            for (auto nn: N) {

                auto key = std::make_pair(n,nn);

                branch_degree = branchdeg.find(key)->second;

                f = branch_best_face.find(key)->second;

                v = branch_best_vertex.find(key)->second;

                if(valency(m,v) == branch_degree) {

                    HalfEdgeID ref_he;
                    VertexID ref_v;

                    for(Walker w = m.walker(v); !w.full_circle(); w = w.circulate_vertex_ccw()) {
                        ref_he = w.halfedge();
                        if(m.walker(ref_he).vertex() == v)
                            ref_v = m.walker(ref_he).opp().vertex();
                        else if (m.walker(ref_he).opp().vertex() == v)
                            ref_v = m.walker(ref_he).vertex();
                    }

                    FaceID f = m.merge_one_ring(v);

                    if(m.in_use(ref_v)) {
                        one_ring_vertex[key] = ref_v;
                        one_ring_face_vertex[f] = ref_v;
                    }
                    else {
                        one_ring_vertex[key] = InvalidVertexID;
                        one_ring_face_vertex[f] = InvalidVertexID;
                    }

                    branchface.insert(std::make_pair(key,f));
                    branch_best_vertex[key] = InvalidVertexID;
                    continue;

                }

                branchface.insert(std::make_pair(key,f));

                for(int i = 0; i < branch_degree - 1; i++) {

                    Walker w_f = m.walker(f);
                    Walker w_h = m.walker(w_f.halfedge());


                    HalfEdgeID w_start = w_f.halfedge();

                    do {

                        if(w_h.vertex() == v) {
                            boundary_edge_1 = w_h.halfedge();
                        }
                        if(w_h.opp().vertex() == v) {
                            boundary_edge_2 = w_h.halfedge();
                        }
                        w_h=w_h.next();

                    } while(w_h.halfedge() != w_start);

                    Walker b_e_1 = m.walker(boundary_edge_1);
                    Walker b_e_2 = m.walker(boundary_edge_2);
                    face_1 = b_e_1.opp().face();
                    face_2 = b_e_2.opp().face();


                    if( ! m.in_use(face_1)) {
                        m.merge_faces(f, boundary_edge_2);
                        continue;
                    }
                    if( ! m.in_use(face_2)) {
                        m.merge_faces(f, boundary_edge_1);
                        continue;
                    }


                    if(face_dist(m, g, n, nn, face_1) < face_dist(m, g, n, nn, face_2))
                        m.merge_faces(f, boundary_edge_1);
                    else
                        m.merge_faces(f, boundary_edge_2);

                    }
              }
        }

    }

    return;

}

//Bridging Functions

FaceID rotate_bridge_face_set(HMesh::Manifold& m, FaceID f0, FaceID f1, Geometry::AMGraph3D& g, NodeID n, NodeID nn) {

    FaceID best_face = f0;

    VertexID central_vertex_0 = face_vertex[f0];
    VertexID central_vertex_1 = face_vertex[f1];

    int central_valency = valency(m, central_vertex_0);

    float min_len = FLT_MAX;

    Vec3d pn = g.pos[n];
    Vec3d pnn = g.pos[nn];
    Vec3d v_n_nn = normalize(pn - pnn);

    float max_dot_sum = -10000;


    for(int iter = 0; iter <= 2*central_valency; iter++) {

        vector<VertexID> vloop0;
        circulate_face_ccw(m, f0, std::function<void(VertexID)>([&](VertexID v){
            vloop0.push_back(v);
        }) );

        vector<VertexID> vloop1;
        circulate_face_ccw(m, f1, std::function<void(VertexID)>([&](VertexID v){
            vloop1.push_back(v);
        }) );

        HalfEdgeID bd_edge;
        circulate_face_ccw(m, f0, std::function<void(HalfEdgeID)>([&](HalfEdgeID h){
            if(m.walker(h).vertex() == central_vertex_0)
                bd_edge = h;
        }) );

        size_t L= vloop0.size();
        VertexID split_vertex;

        for(int i = 0; i < L; i++) {
            if(vloop0[i] == central_vertex_0)
                split_vertex = vloop0[(i+3)%L];
        }

        int j_off_min_len = -1;
        float dot_sum = 0;
        float len = 0;
        for(int j_off = 0; j_off < L; ++j_off) {
            bool center_match = false;
            for(int i=0;i<L;++i) {
                if(vloop0[i] == central_vertex_0 && vloop1[(L + j_off - i)%L] == central_vertex_1)
                    center_match = true;

            }
            if(center_match) {
                len = 0;
                dot_sum = 1000;
                for(int i=0;i<L;++i) {
                    len += sqr_length(m.pos(vloop0[i]) - m.pos(vloop1[(L+j_off - i)%L]));
                    Vec3d bridge_edge = normalize(m.pos(vloop0[i]) - m.pos(vloop1[(L+j_off - i)%L]));
                    if(abs(dot(v_n_nn, bridge_edge)) < dot_sum)
                        dot_sum = abs(dot(v_n_nn, bridge_edge)); ///len;
                }
                dot_sum = 10000;
                Vec3d bridge_edge_i, bridge_edge_j;

                for(int i=0;i<L;i++) {
                    bridge_edge_i = normalize(m.pos(vloop0[i]) - m.pos(vloop1[(L+j_off - i)%L]));

                    for(int j=0;j<L;j++) {


                        bridge_edge_j = normalize(m.pos(vloop0[j]) - m.pos(vloop1[(L+j_off - j)%L]));
                        double curr_dot_sum = dot(bridge_edge_i, bridge_edge_j);
                        if(curr_dot_sum < dot_sum)
                          dot_sum = curr_dot_sum;
                    }
                }

            }
        }


        dot_sum = dot_sum;
        if(iter < central_valency){
            if(dot_sum > max_dot_sum) {
                max_dot_sum = dot_sum;
                }
        }
        else if (iter >= central_valency){
            if(dot_sum == max_dot_sum) {
                return f0;
             }
       }

       FaceID new_face = f0;
       if(L == 4) {
           if(m.walker(bd_edge).face() == f0)
               new_face = m.walker(bd_edge).opp().face();
           else
               new_face = m.walker(bd_edge).face();
       }
       else {
       new_face = m.split_face_by_edge(f0, central_vertex_0, split_vertex);

       if(m.in_use(bd_edge))
           m.merge_faces(m.walker(bd_edge).face(),bd_edge);
      }


        f0 = new_face;

    }
    return f0;
}

vector<pair<VertexID, VertexID>> face_match_careful(HMesh::Manifold& m, FaceID &f0, FaceID &f1, Geometry::AMGraph3D& g, NodeID n, NodeID nn) {

    vector<pair<VertexID, VertexID> > connections;
    if(!m.in_use(f0) || !m.in_use(f1))
        return connections;

    VertexID v0 = face_vertex[f0];
    VertexID v1 = face_vertex[f1];


    vector<VertexID> loop0;
    circulate_face_ccw(m, f0, std::function<void(VertexID)>([&](VertexID v){
        loop0.push_back(v);
    }));

    vector<VertexID> loop1;
    circulate_face_ccw(m, f1, std::function<void(VertexID)>( [&](VertexID v) {
        loop1.push_back(v);
    }));

    size_t L0= loop0.size();
    size_t L1= loop1.size();

    if (L0 != L1)
        return connections;

    NodeID start_node = next_jn(g, nn, n);
    NodeID end_node = next_jn(g, n, nn);

    size_t L = L0;

    if(face_vertex[f0] == InvalidVertexID || face_vertex[f1] == InvalidVertexID) {

        float min_len = FLT_MAX;
        int j_off_min_len = -1;
        for(int j_off = 0; j_off < L; j_off = j_off + 1) {
            float len = 0;
            for(int i=0;i<L;++i)
            len += sqr_length(m.pos(loop0[i]) - m.pos(loop1[(L+j_off - i)%L]));
            if(len < min_len)   {
                j_off_min_len = j_off;
                min_len = len;
            }
        }

        for (int i = 0; i < L; i++) {

            VertexID v0 = loop0[i];
            VertexID v1 = loop1[(L + j_off_min_len - i)%L];

            if(face_vertex[f1] == v1) {
                Walker w = m.walker(f0);
                face_vertex[f0] = v0;
                face_vertex[m.walker(w.halfedge()).opp().face()] = v0;
            }

            else if (face_vertex[f0] == v0) {

                Walker w = m.walker(f1);
                face_vertex[f1] = v1;
                face_vertex[m.walker(w.halfedge()).opp().face()] = v1;
            }

        }

    for(int i=0;i<L;++i)
    connections.push_back(pair<VertexID, VertexID>(loop0[i],loop1[(L+ j_off_min_len - i)%L]));


    }

    else {


        VertexID central_vertex_0 = face_vertex[f0];
        VertexID central_vertex_1 = face_vertex[f1];

        FaceID new_face = rotate_bridge_face_set(m, f0, f1, g, n, nn);


        if(new_face != InvalidFaceID)
            f0 = new_face;
        else {
            return connections;
        }

        if(f0 == InvalidFaceID) {
            return connections;
        }


        loop0.clear();
        circulate_face_ccw(m, f0, std::function<void(VertexID)>([&](VertexID v){
            loop0.push_back(v);
        }) );

        loop1.clear();
        circulate_face_ccw(m, f1, std::function<void(VertexID)>( [&](VertexID v) {
            loop1.push_back(v);
        }) );

        int j_off_min_len = -1;
        for(int j_off = 0; j_off < L; j_off = j_off + 1) {
            bool center_match = false;
            for(int i=0;i<L;++i) {
                if(loop0[i] == central_vertex_0 && loop1[(L + j_off - i)%L] == central_vertex_1)
                    center_match = true;

            }
            if(center_match) {
                j_off_min_len = j_off;
            }
        }

    for(int i=0;i<L;++i)
        connections.push_back(pair<VertexID, VertexID>(loop0[i],loop1[(L+ j_off_min_len - i)%L]));

    }


    return connections;
}

vector<pair<VertexID, VertexID>> face_match_one_ring(HMesh::Manifold& m, FaceID &f0, FaceID &f1) {

    vector<pair<VertexID, VertexID> > connections;
    if(!m.in_use(f0) || !m.in_use(f1))
        return connections;

    VertexID face_vertex_0 = one_ring_face_vertex[f0];
    VertexID face_vertex_1 = one_ring_face_vertex[f1];

    bool fv_flag = false;

    if(face_vertex_0 != InvalidVertexID && face_vertex_1 != InvalidVertexID)
      fv_flag = true;

    int loop0_index = 0, loop1_index = 0;

    vector<VertexID> loop0;

    int count = 0;

    circulate_face_ccw(m, f0, std::function<void(VertexID)>([&](VertexID v){
        loop0.push_back(v);
        if(v == face_vertex_0)
          loop0_index = count;
        count++;
    }) );

    vector<VertexID> loop1;
    count = 0;

    circulate_face_ccw(m, f1, std::function<void(VertexID)>( [&](VertexID v) {
        loop1.push_back(v);
        if(v == face_vertex_1)
          loop1_index = count;
        count++;
    }) );

    size_t L0= loop0.size();
    size_t L1= loop1.size();

    if (L0 != L1)
        return connections;

    size_t L = L0;

    float min_len = FLT_MAX;
    int j_off_min_len = -1;
    float max_dot_sum = -10000;

    for(int j_off = 0; j_off < L; j_off = j_off + 1) {
        Vec3d bridge_edge_i, bridge_edge_j;
        float dot_sum = 100000;
        float len = 0;

        for(int i=0;i<L;++i) {
          len += sqr_length(m.pos(loop0[i]) - m.pos(loop1[(L+j_off - i)%L]));
        }
           if(len < min_len)   {
            j_off_min_len = j_off;
            min_len = len;
           }
  }

    int found_flag = 0;
    for(int i=0;i<L;++i)
        connections.push_back(pair<VertexID, VertexID>(loop0[i],loop1[(L+ j_off_min_len - i)%L]));

    if(one_ring_face_vertex[f0] != InvalidVertexID && one_ring_face_vertex[f1] != InvalidVertexID)
        return connections;

    for (int i = 0; i < L; i++) {

        VertexID v0 = loop0[i];
        VertexID v1 = loop1[(L + j_off_min_len - i)%L];

        if(face_vertex_1 == v1) {
            Walker w = m.walker(f0);
            one_ring_face_vertex[f0] = v0;
            one_ring_face_vertex[m.walker(w.halfedge()).opp().face()] = v0;
            found_flag = 1;
        }

        else if (face_vertex_0 == v0) {
            Walker w = m.walker(f1);
            one_ring_face_vertex[f1] = v1;
            one_ring_face_vertex[m.walker(w.halfedge()).opp().face()] = v1;
            found_flag = 1;
        }
    }

    if(found_flag == 0 && (one_ring_face_vertex[f0] != InvalidVertexID || one_ring_face_vertex[f1] != InvalidVertexID)) {
        connections.clear();
        return connections;
    }



    return connections;
}

FaceID find_bridge_face(HMesh::Manifold &m_out, Geometry::AMGraph3D& g, NodeID start_node, NodeID next_node, Util::AttribVec<NodeID, FaceSet>& node2fs) {

  FaceID f0;

  auto best_face = [&](NodeID n, NodeID nn) {
      Vec3d pn = g.pos[n];
      Vec3d pnn = g.pos[nn];
      Vec3d v_n_nn = pnn - pn;
      double d_max = -1000;
      FaceID f_max = InvalidFaceID;
      v_n_nn = normalize(v_n_nn);
      for(auto f: node2fs[n]) {
          double d = dot(v_n_nn, normal(m_out, f));
          if(d> d_max) {
              f_max = f;
              d_max = d;
          }

      }
      node2fs[n].erase(f_max);
      return f_max;
  };

  if(g.neighbors(start_node).size()>2)  {

    auto key = std::make_pair(start_node,next_node);
    f0 = branchface.find(key)->second;
    VertexID v0 = branch_best_vertex.find(key)->second;
    if(v0 != InvalidVertexID)
        face_vertex[f0] = v0;

  }
  else
    f0 = best_face(start_node,next_node);

  return f0;

}

vector<pair<VertexID, VertexID>> find_bridge_connections(HMesh::Manifold &m_out, FaceID &f0, FaceID &f1, Geometry::AMGraph3D& g, NodeID n, NodeID nn) {
  vector<pair<VertexID, VertexID>> connections;

  if(f0 == InvalidFaceID || f1 == InvalidFaceID)
    return connections;

  if(face_vertex[f0] == InvalidVertexID && face_vertex[f1] == InvalidVertexID)
    connections = face_match_one_ring(m_out, f0, f1);

  else if (f0 != InvalidFaceID && f1 != InvalidFaceID)
    connections = face_match_careful(m_out, f0, f1, g, n, nn);

  return connections;

}

//Setup Global arrays

void init_graph_arrays(HMesh::Manifold &m_out, Geometry::AMGraph3D& g, Util::AttribVec<NodeID, FaceSet>& node2fs) {
  init_branch_degree(m_out, g, node2fs);

  init_branch_face_pairs(m_out, g, node2fs);

  merge_branch_faces(m_out, g, node2fs);
}

//Main functions

HMesh::Manifold graph_to_FEQ(Geometry::AMGraph3D& g) {

    double r = 0.5 * g.average_edge_length();
    Manifold m_out;
    Util::AttribVec<NodeID, FaceSet> node2fs;

    clear_global_arrays();

    construct_bnps(m_out,g, node2fs, r);

    init_graph_arrays(m_out, g, node2fs);

    FaceAttributeVector<int> ftouched(m_out.allocated_faces(),-1);

    val2nodes_to_boxes(g, m_out, node2fs, r);


    for(auto f_id: m_out.faces()) {
        face_vertex[f_id] = InvalidVertexID;
        if(one_ring_face_vertex.find(f_id) == one_ring_face_vertex.end())
            one_ring_face_vertex[f_id] = InvalidVertexID;
    }

//    branchface.end()->second = InvalidFaceID;

    bool debug_break = false;

    bool has_junction = false;

    for (auto n: g.node_ids()) {
      if(g.valence(n) > 2)
        has_junction = true;
    }

    for (auto n: g.node_ids()) {

        FaceID f0 = InvalidFaceID;
        FaceID f1 = InvalidFaceID;
        VertexID v0, v1;

        auto N = g.neighbors(n);

        if (N.size()<=2 && has_junction)
            continue;

        for(auto nn: N) {

            auto key = std::make_pair(n,nn);
            f0 = branchface.find(key)->second;

            VertexID central_v = branch_best_vertex.find(key)->second;



            if(branchdeg.find(key)->second < 1 && has_junction)
                continue;

            NodeID start_node = n;
            NodeID next_node = nn;

            vector<NodeID> nbd_list = next_neighbours(g, start_node, next_node);

            do {

                FaceID f0 = find_bridge_face(m_out, g, start_node, next_node, node2fs);

                FaceID f1 = find_bridge_face(m_out, g, next_node, start_node, node2fs);

                nbd_list = next_neighbours(g, start_node, next_node);

                if(g.valence(next_node) > g.valence(start_node)) {
                  auto connections = find_bridge_connections(m_out, f1, f0, g, n, nn);
                  if(connections.size()!=0) {
                    m_out.bridge_faces(f1,f0,connections);
                    ftouched[f0] = 1;
                    ftouched[f1] = 1;
                  }
                }
                else {
                  auto connections = find_bridge_connections(m_out, f0, f1, g, n, nn);
                  if(connections.size()!=0) {
                    m_out.bridge_faces(f0,f1,connections);
                    ftouched[f0] = 1;
                    ftouched[f1] = 1;
                  }
                }

                start_node = next_node;
                if(nbd_list.size()==1)
                  next_node = nbd_list[0];

            }  while(nbd_list.size()==1);
        }
    }

    quad_mesh_leaves(m_out);
    return m_out;
}

HMesh::Manifold graph_to_FEQ_radius(Geometry::AMGraph3D& g, vector<double> node_radii) {

    double r = 0.5 * g.average_edge_length();
    Manifold m_out;
    Util::AttribVec<NodeID, FaceSet> node2fs;

    clear_global_arrays();

    construct_bnps_radius(m_out, g, node2fs, node_radii);

    init_graph_arrays(m_out, g, node2fs);

    FaceAttributeVector<int> ftouched(m_out.allocated_faces(),-1);

    val2nodes_to_boxes_radius(g, m_out, node2fs, node_radii);

    for(auto f_id: m_out.faces()) {
        face_vertex[f_id] = InvalidVertexID;
        if(one_ring_face_vertex.find(f_id) == one_ring_face_vertex.end())
            one_ring_face_vertex[f_id] = InvalidVertexID;
    }

//    branchface.end()->second = InvalidFaceID;

    bool has_junction = false;

    for (auto n: g.node_ids()) {
      if(g.valence(n) > 2)
        has_junction = true;
    }

    for (auto n: g.node_ids()) {

        FaceID f0 = InvalidFaceID;
        FaceID f1 = InvalidFaceID;
        VertexID v0, v1;

        auto N = g.neighbors(n);

        if (N.size()<=2 && has_junction)
            continue;

        for(auto nn: N) {

            auto key = std::make_pair(n,nn);
            f0 = branchface.find(key)->second;

            VertexID central_v = branch_best_vertex.find(key)->second;



            if(branchdeg.find(key)->second < 1 && has_junction)
                continue;

            NodeID start_node = n;
            NodeID next_node = nn;

            vector<NodeID> nbd_list = next_neighbours(g, start_node, next_node);

            do {

                FaceID f0 = find_bridge_face(m_out, g, start_node, next_node, node2fs);

                FaceID f1 = find_bridge_face(m_out, g, next_node, start_node, node2fs);

                nbd_list = next_neighbours(g, start_node, next_node);


                if(g.valence(next_node) > g.valence(start_node)) {
                  auto connections = find_bridge_connections(m_out, f1, f0, g, n, nn);
                  if(connections.size()!=0) {
                    m_out.bridge_faces(f1,f0,connections);
                    ftouched[f0] = 1;
                    ftouched[f1] = 1;
                  }
                }
                else {
                  auto connections = find_bridge_connections(m_out, f0, f1, g, n, nn);
                  if(connections.size()!=0) {
                    m_out.bridge_faces(f0,f1,connections);
                    ftouched[f0] = 1;
                    ftouched[f1] = 1;
                  }
                }

                start_node = next_node;
                if(nbd_list.size()==1)
                  next_node = nbd_list[0];

            }  while(nbd_list.size()==1);
        }
    }

    quad_mesh_leaves(m_out);
    return m_out;
}
