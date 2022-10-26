# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

from torch.utils._pytree import tree_flatten, tree_unflatten


def tree_map_(fn_, pytree, grad_fn=False):
    flat_args, spec = tree_flatten(pytree, grad_fn=grad_fn)
    [fn_(arg) for arg in flat_args]
    return pytree


class PlaceHolder():
    def __repr__(self):
        return '*'


def treespec_pprint(spec):
    leafs = [PlaceHolder() for _ in range(spec.num_leaves)]
    result = tree_unflatten(leafs, spec)
    return repr(result)
