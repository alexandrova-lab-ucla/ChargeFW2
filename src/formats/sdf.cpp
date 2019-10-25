//
// Created by krab1k on 24/10/18.
//

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <set>
#include <vector>
#include <memory>
#include <fmt/format.h>

#include "chargefw2.h"
#include "../structures/atom.h"
#include "../structures/bond.h"
#include "../structures/molecule.h"
#include "../periodic_table.h"
#include "common.h"
#include "sdf.h"


void read_V2000(std::ifstream &file, std::string &line, std::unique_ptr<std::vector<Atom>> &atoms,
                std::unique_ptr<std::vector<Bond>> &bonds);

void read_V3000(std::ifstream &file, std::string &line, std::unique_ptr<std::vector<Atom>> &atoms,
                std::unique_ptr<std::vector<Bond>> &bonds);

void read_until_end_of_record(std::ifstream &file);


void read_until_end_of_record(std::ifstream &file) {
    std::string line;
    do {
        std::getline(file, line);
    } while (line != "$$$$");
}


MoleculeSet SDF::read_file(const std::string &filename) {
    std::ifstream file(filename);
    if (!file) {
        fmt::print(stderr, "Cannot open file: {}\n", filename);
        exit(EXIT_FILE_ERROR);
    }

    std::string line;
    std::string name;

    std::set<std::string> molecule_names;

    auto molecules = std::make_unique<std::vector<Molecule>>();
    while (std::getline(file, line)) {
        try {
            name = line; // Read name of the molecule
            if (name.length() > 80) {
                throw std::runtime_error("Name of the molecule in SDF must have at most 80 characters");
            }

            name = sanitize_name(name.substr(0, 80));
            name = get_unique_name(name, molecule_names);
            molecule_names.insert(name);

            std::getline(file, line); // Line with comments
            std::getline(file, line); // Line with comments

            std::getline(file, line); // Line with counts

            std::string version;
            try {
                version = line.substr(34, 5);
            }
            catch (std::exception &) {
                version = "";
            }

            auto atoms = std::make_unique<std::vector<Atom>>();
            auto bonds = std::make_unique<std::vector<Bond>>();

            if (version == "V2000") {
                read_V2000(file, line, atoms, bonds);
                molecules->emplace_back(name, std::move(atoms), std::move(bonds));
            } else if (version == "V3000") {
                read_V3000(file, line, atoms, bonds);
                molecules->emplace_back(name, std::move(atoms), std::move(bonds));
            } else {
                throw std::runtime_error(fmt::format("Invalid MOL version \"{}\"", version));
            }
        } catch (std::exception &e) {
            fmt::print(stderr, "Cannot load structure {} from SDF file: {}\n", name, e.what());
            read_until_end_of_record(file);
        }
    }

    return MoleculeSet(std::move(molecules));

}


void read_V2000(std::ifstream &file, std::string &line, std::unique_ptr<std::vector<Atom>> &atoms,
                std::unique_ptr<std::vector<Bond>> &bonds) {

    size_t n_atoms = std::stoul(line.substr(0, 3));
    size_t n_bonds = std::stoul(line.substr(3, 3));

    atoms->reserve(n_atoms);

    for (size_t i = 0; i < n_atoms; i++) {
        std::getline(file, line);
        double x = std::stod(line.substr(0, 10));
        double y = std::stod(line.substr(10, 10));
        double z = std::stod(line.substr(20, 10));

        auto element = PeriodicTable::pte().get_element_by_symbol(get_element_symbol(line.substr(31, 3)));

        atoms->emplace_back(i, element, x, y, z, element->symbol(), 0, "UNL", "", false);
    }

    bonds->reserve(n_bonds);

    for (size_t i = 0; i < n_bonds; i++) {
        std::getline(file, line);
        size_t first = std::stoul(line.substr(0, 3));
        size_t second = std::stoul(line.substr(3, 3));
        int order = std::stoi(line.substr(6, 3));

        bonds->emplace_back(&((*atoms)[first - 1]), &((*atoms)[second - 1]), order);
    }

    do {
        std::getline(file, line);
        if (line.substr(0, 6) == "M  CHG") {
            size_t count = std::stoul(line.substr(6, 3));
            const size_t base = 9;
            for (size_t i = 0; i < count; i++) {
                size_t atom_no = std::stoul(line.substr(base + i * 8, 4));
                int charge = std::stoi(line.substr(base + i * 8 + 4, 4));
                (*atoms)[atom_no - 1]._set_formal_charge(charge);
            }
        }
    } while (line != "$$$$");
}


void read_V3000(std::ifstream &file, std::string &line, std::unique_ptr<std::vector<Atom>> &atoms,
                std::unique_ptr<std::vector<Bond>> &bonds) {
    /* Skip 'M  V30 BEGIN CTAB' line */
    std::getline(file, line);

    /* Read atom & bond counts */
    std::getline(file, line);
    size_t n_atoms;
    size_t n_bonds;
    std::stringstream ss(line.substr(14));
    ss >> n_atoms >> n_bonds;

    atoms->reserve(n_atoms);

    /* Skip 'M  V30 BEGIN ATOM' line */
    std::getline(file, line);

    /* Read info about the atoms */
    for (size_t i = 0; i < n_atoms; i++) {
        std::getline(file, line);

        ss.str(line.substr(7));
        ss.clear();

        std::string symbol;
        size_t idx;
        double x, y, z;
        int formal_charge = 0;
        ss >> idx >> symbol >> x >> y >> z;

        /* Search for charge data */
        auto pos = line.find("CHG=");
        if (pos != std::string::npos) {
            ss.str(line.substr(pos + 4));
            ss.clear();
            ss >> formal_charge;
        }

        /* Should we continue to next line */
        while (line.back() == '-') {
            std::getline(file, line);
            pos = line.find("CHG=");
            if (pos != std::string::npos) {
                ss.str(line.substr(pos + 4));
                ss.clear();
                ss >> formal_charge;
            }
        }
        auto element = PeriodicTable::pte().get_element_by_symbol(get_element_symbol(symbol));

        atoms->emplace_back(i, element, x, y, z, element->symbol(), 0, "UNL", "", false);
        atoms->back()._set_formal_charge(formal_charge);
    }

    /* Skip 'M  V30 END ATOM' line */
    std::getline(file, line);

    /* Skip 'M  V30 BEGIN BOND' line */
    std::getline(file, line);

    bonds->reserve(n_bonds);

    for (size_t i = 0; i < n_bonds; i++) {
        std::getline(file, line);
        ss.str(line.substr(7));
        ss.clear();
        size_t idx, first, second;
        int order;
        ss >> idx >> order >> first >> second;

        bonds->emplace_back(&((*atoms)[first - 1]), &((*atoms)[second - 1]), order);
    }

    /* Skip everything until the end of the record */
    read_until_end_of_record(file);
}
