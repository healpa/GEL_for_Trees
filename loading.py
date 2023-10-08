#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Thu Oct  5 07:50:36 2023

@author: healpa

"""


# Script to load the raw skeleton to test newly implemented functions

from pygel3d import hmesh, graph, gl_display as gl
#from numpy import array
#from numpy.linalg import norm
#from scipy.spatial import KDTree
from time import time
#from math import sqrt
 
#
t0 = time()
s = graph.load("skel_compl.graph")
# V = gl.Viewer()
# V.display(s)
print("Loaded skeleton")
print(f"Time elapsed: {time()-t0:.5} seconds")

#Testing coloring detached parts 
t0 = time()
m = graph.color_detached_parts(s)
print("Colored detached parts of the skeleton")
print(f"Time elapsed: {time()-t0:.5} seconds")
V = gl.Viewer()
V.display(m)

## Iso surface testing
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

## Cylinder surface testing
# t0 = time()
# m = graph.to_mesh_cyl(s, fudge=0.01)
# print("Made iso mesh")
# print(f"Time elapsed: {time()-t0:.5} seconds")

