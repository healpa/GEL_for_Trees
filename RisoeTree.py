from pygel3d import hmesh, graph, gl_display as gl
from numpy import array
from numpy.linalg import norm
from scipy.spatial import KDTree
from time import time
from math import sqrt


 
t0 = time()
f = open("/Users/healpa/Documents/git/GEL_tree_geometries/data/point_clouds/WinterTree_pts_clean_filtered.off")
#f = open("/Users/healpa/Documents/git/GEL_tree_geometries/data/point_clouds/Quercus_rubra.txt")

lns = f.readlines()
g = graph.Graph()
pts = []
for l in lns[2:]:
    p = array([ float(num) for num in l.split() ])
    pts += [p]
    g.add_node(p)
print(f"Tree has {len(g.nodes())} nodes")
print(f"Time elapsed: {time()-t0:.5} seconds\n")
 
t0 = time()
pts = array(pts)
T = KDTree(pts)
print("Made KDTree")
print(f"Time elapsed: {time()-t0:.5} seconds\n")
 
t0 = time()
conn = 0
for i,j in T.query_pairs(r=0.025):
    g.connect_nodes(i,j)
    conn += 1
print(f"Connected {conn} nodes")
print(f"Time elapsed: {time()-t0:.5} seconds\n")
 
t0 = time()
graph.edge_contract(g, dist_thresh=0.01)
print("Contracted edges")
print(f"Time elapsed: {time()-t0:.5} seconds\n")
 
t0 = time()
graph.saturate(g, hops=10, rad=0.05, dist_frac=0.95)
print("Saturated graph")
print(f"Time elapsed: {time()-t0:.5} seconds\n")

#V = gl.Viewer()
#V.display(g, bg_col=[1,1,1])
 
 
t0 = time()
s = graph.LS_skeleton(g, True)
print("Made skeleton")
print(f"Time elapsed: {time()-t0:.5} seconds")

# t0 = time()
# s = graph.MSLS_skeleton(g, grow_thresh=128)
# print("Made skeleton")
# print(f"Time elapsed: {time()-t0:.5} seconds")


t0 = time()
sc = graph.color_detached_parts(s)
print("Colored detached parts of the skeleton")
print(f"Time elapsed: {time()-t0:.5} seconds")
V = gl.Viewer()
V.display(sc, bg_col=[1,1,1])
graph.save("tree_skel_col.graph", sc)
print("Saved graph")

# t0 = time()
# m = graph.to_mesh_iso(s, fudge=0.01, res=1024)
# print("Made iso mesh")
# print(f"Time elapsed: {time()-t0:.5} seconds")
 
# t0 = time()
# hmesh.quadric_simplify(m, 0.05)
# print("Simplified mesh")
# print(f"Time elapsed: {time()-t0:.5} seconds")
# V.display(m)
# hmesh.save("tree_iso_reduced.obj", m)