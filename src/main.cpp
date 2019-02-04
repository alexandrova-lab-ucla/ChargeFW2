#include <fmt/format.h>
#include <fmt/ostream.h>
#include <boost/dll/import.hpp>
#include <memory>
#include <ctime>
#include <algorithm>
#include <filesystem>

#include "chargefw2.h"
#include "formats/reader.h"
#include "formats/sdf.h"
#include "formats/mol2.h"
#include "formats/pdb.h"
#include "formats/pqr.h"
#include "formats/mmcif.h"
#include "formats/txt.h"
#include "structures/molecule_set.h"
#include "parameters.h"
#include "charges.h"
#include "method.h"
#include "config.h"
#include "parameterization.h"


std::string best_parameters(MoleculeSet &ms, const std::shared_ptr<Method> &method);

std::shared_ptr<Method> load_method(const std::string &method_name);


int main(int argc, char **argv) {
    auto parsed = parse_args(argc, argv);
    check_common_args();

    auto ext = std::filesystem::path(config::input_file).extension();

    bool protein_structure = false;
    std::unique_ptr<Reader> reader;
    if (ext == ".sdf") {
        reader = std::make_unique<SDF>();
    } else if (ext == ".mol2") {
        reader = std::make_unique<Mol2>();
    } else if (ext == ".pdb") {
        reader = std::make_unique<PDB>();
        protein_structure = true;
    } else if (ext == ".cif") {
        reader = std::make_unique<mmCIF>();
        protein_structure = true;
    } else {
        fmt::print(stderr, "Filetype {} not supported\n", ext);
        exit(EXIT_FILE_ERROR);
    }

    MoleculeSet m = reader->read_file(config::input_file);

    if (config::mode == "info") {
        m.classify_atoms(AtomClassifier::HBO);
        m.info();

    } else if (config::mode == "charges") {
        std::shared_ptr<Method> method = load_method(config::method_name);

        setup_method_options(method, parsed);

        auto p = std::unique_ptr<Parameters>();

        if (method->has_parameters()) {
            std::string par_name;
            if (config::par_file.empty()) {
                par_name = best_parameters(m, method);
                if (par_name.empty()) {
                    fmt::print(stderr, "No parameters found \n");
                    exit(EXIT_PARAMETER_ERROR);
                }
                fmt::print("Best parameters found: {}\n", par_name);
            } else {
                par_name = config::par_file;
            }

            p = std::make_unique<Parameters>(par_name);

            fmt::print("Parameters:\n");
            p->print();

            size_t unclassified = m.classify_set_from_parameters(*p);
            fmt::print("\nNumber of unclassified molecules: {}\n\n", unclassified);

            method->set_parameters(p.get());
        } else {
            m.classify_atoms(AtomClassifier::PLAIN);
        }

        m.info();
        fmt::print("\n");

        m.fulfill_requirements(method->get_requirements());

        auto charges = Charges();

        charges.set_method_name(config::method_name);

        clock_t begin = clock();

        for (auto &mol: m.molecules()) {
            charges.insert(mol.name(), method->calculate_charges(mol));
        }

        clock_t end = clock();

        fmt::print("Computation took {:.2f} seconds\n", double(end - begin) / CLOCKS_PER_SEC);

        auto txt = TXT();
        std::filesystem::path dir(config::chg_out_dir);
        std::filesystem::path file(config::input_file);
        auto txt_str = file.filename().string() + ".txt";
        txt.save_charges(m, charges, dir / std::filesystem::path(txt_str));

        if (protein_structure) {
            auto pqr = PQR();
            auto pqr_str = file.filename().string() + ".pqr";
            pqr.save_charges(m, charges, dir / std::filesystem::path(pqr_str));
        } else {
            auto mol2 = Mol2();
            auto mol2_str = file.filename().string() + ".mol2";
            mol2.save_charges(m, charges, dir / std::filesystem::path(mol2_str));
        }

    } else if (config::mode == "best-parameters") {
        std::shared_ptr<Method> method = load_method(config::method_name);

        if (!method->has_parameters()) {
            fmt::print(stderr, "Method uses no parameters\n");
            exit(EXIT_PARAMETER_ERROR);
        }

        fmt::print("Best parameters are: {}\n", best_parameters(m, method));

    } else if (config::mode == "parameters") {
        m.classify_atoms(AtomClassifier::PLAIN);

        std::shared_ptr<Method> method = load_method(config::method_name);

        setup_method_options(method, parsed);

        Charges reference_charges(config::ref_chg_file);

        auto p = Parameterization(m, method, reference_charges, config::chg_out_dir, config::par_file);
        p.parametrize();

    } else {
        fmt::print(stderr, "Unknown mode {}\n", config::mode);
        exit(EXIT_PARAMETER_ERROR);
    }

    return 0;
}


std::string best_parameters(MoleculeSet &ms, const std::shared_ptr<Method> &method) {
    std::string best_name;
    size_t best_unclassified = ms.molecules().size();

    for (const auto &set: std::filesystem::directory_iterator(std::string(INSTALL_DIR) + "/share/parameters")) {
        auto p = std::make_unique<Parameters>(set.path());

        if (method->name() != p->method_name())
            continue;

        size_t unclassified = ms.classify_set_from_parameters(*p, false);

        // If all molecules are covered by the parameters, we found our best
        if (!unclassified) {
            return set.path();
        }

        if (unclassified < best_unclassified) {
            best_unclassified = unclassified;
            best_name = set.path();
        }
    }

    return best_name;
}


std::shared_ptr<Method> load_method(const std::string &method_name) {
    try {
        auto ptr = boost::dll::import<Method>(std::string(INSTALL_DIR) + "/lib/" + method_name, "method",
                                              boost::dll::load_mode::append_decorations);
        /* Some magic from: https://stackoverflow.com/a/12315035/2693542 */
        return std::shared_ptr<Method>(ptr.get(), [ptr](Method *) mutable { ptr.reset(); });
    } catch (std::exception &) {
        fmt::print(stderr, "Unable to load method {}\n", method_name);
        exit(EXIT_PARAMETER_ERROR);
    }
}
