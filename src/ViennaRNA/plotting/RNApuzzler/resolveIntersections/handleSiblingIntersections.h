#ifndef SIBLING_INTERSCTIONS_H
#define SIBLING_INTERSCTIONS_H

#include "ViennaRNA/plotting/RNApuzzler/dataTypes/configtree_struct.h"
#include "ViennaRNA/plotting/RNApuzzler/definitions.h"

/**
 * Check and fix intersections between all siblings of the current node.
 *
 * @param node: node to check
 * @param puzzler: puzzlerOptions
 */
short checkSiblings(
        treeNode* node,
        puzzlerOptions* puzzler
);


#endif
