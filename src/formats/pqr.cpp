//
// Created by krab1k on 31.1.19.
//

#include <string>

#include "pqr.h"
#include "../structures/molecule_set.h"
#include "../charges.h"
#include "config.h"


void PQR::save_charges(const MoleculeSet &ms, const Charges &charges, const std::string &filename) {
    auto file = std::fopen(filename.c_str(), "w");
    if (!file) {
        fmt::print(stderr, "Cannot open file: {}\n", filename);
        exit(EXIT_FILE_ERROR);
    }

    const auto &molecule = ms.molecules()[0];

    try {
        auto chg = charges[molecule.name()];
        for (size_t i = 0; i < molecule.atoms().size(); i++) {
            const auto &atom = molecule.atoms()[i];
            std::string extra_space;
            if (atom.element().symbol().length() == 1) {
                extra_space = " ";
            }
            fmt::print(file, "ATOM  {:>5d} {}{:<3s} {:>3s} {:1s} {:>3d}    {:>8.3f}{:>8.3f}{:>8.3f} {:>6.3f} {:>6.3f}\n",
                       i + 1, extra_space, atom.name(), atom.residue(), atom.chain_id(), atom.residue_id(),
                       atom.pos()[0], atom.pos()[1], atom.pos()[2], chg[i], atom.element().vdw_radius());
        }
    }
    catch (std::out_of_range &) {
        /* Do nothing */
    }

    fclose(file);
}
