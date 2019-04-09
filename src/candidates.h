//
// Created by krab1k on 8.4.19.
//

#pragma once

#include "structures/molecule_set.h"
#include "method.h"


void get_suitable_methods(MoleculeSet &ms, bool is_protein);

std::string best_parameters(MoleculeSet &ms, const std::shared_ptr<Method> &method, bool is_protein);