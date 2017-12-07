#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cmath>

using namespace std;

int pickRandomAtom(System &system) {
    return floor(getrand()*(double)system.molecules[0].atoms.size());
}

void perturbAtom(System &system, int i, double mf) {
    double rand[3] = {mf*(getrand()*2-1), mf*(getrand()*2-1), mf*(getrand()*2-1)}; // 3 vals from (-1 -> +1)*movefactor
    for (int n=0;n<3;n++)
        system.molecules[0].atoms[i].pos[n] += rand[n];
    return;
}

double move_factor(double energy) {
    // a simple scalar modifier to the explorable volume for optimization
    // the move_factor scales down to 0 as energy approaches 0
    // thus we save some time optimizing by not trying dumb (big) moves 
    // when energy is very low.
    return 1.0 -exp(-energy*energy); // reverse bell-curve
}

// Optimize the molecule (ID=0) via MM forcefield(s)
void optimize(System &system) {

    int i;
    // print out all the bonds
    printf("Dynamically-found Bonds Summary:\n");
    printf("================================\n");
    printf("Elements :: atom-id :: bond-id\n");
    for (i=0; i<system.molecules[0].atoms.size(); i++) {
        printf("Atom %i (UFF: %s) bonds:\n", i, system.molecules[0].atoms[i].UFFlabel.c_str());
        for (std::map<int,int>::iterator it=system.molecules[0].atoms[i].bonds.begin(); it!=system.molecules[0].atoms[i].bonds.end(); ++it) {
            printf("%3s%2s%3s :: %1s%5i%1s :: %1s%5i%1s\n", 
                    system.molecules[0].atoms[i].name.c_str(), "--", system.molecules[0].atoms[it->first].name.c_str(), 
                    " ", it->first, " ",
                    " ", it->second, " ");
        }
    }
    printf("================================\n");

    // iterate monte-carlo style pertubations until
    // the molecular energy is minimized
    int converged = 0;
    double error_tolerance = system.constants.opt_error;
    int step_limit = system.constants.opt_step_limit; //100;
    double Ei = stretch_energy(system) + angle_bend_energy(system);
    double Ef;
    double delta_E;
    double boltzmann;
    double tmp_pos[3] = {0,0,0};
    int randatom;
    int step=0;
    writeXYZ(system, system.constants.output_traj, 0, step, 0, 0);
    printf("Step %i :: Energy = %f; diff = %f kcal/mol; \n", 0, Ei, 0.0);

    int optmode = system.constants.opt_mode;
    // Monte Carlo sytle opt.
    if (optmode == OPTIMIZE_MC) {
    while (!converged) {
        Ei = stretch_energy(system) + angle_bend_energy(system);

        // select random atom and perturb it.
        randatom = pickRandomAtom(system);
        for (int n=0;n<3;n++) tmp_pos[n] = system.molecules[0].atoms[randatom].pos[n];
        perturbAtom(system, randatom, move_factor(Ei));

        // get new energy
        Ef = stretch_energy(system) + angle_bend_energy(system);
        delta_E = Ef - Ei;

      //  printf("Ef after = %f\n", Ef);

        //boltzmann = exp(-delta_E/0.00001); // just for now..
        //if (getrand() < boltzmann) {
        if (delta_E < 0.001) { // allow some positives a la Monte Carlo 
           // accept
            step++;
            writeXYZ(system, system.constants.output_traj, 0, step, 0, 0);
            printf("Step %i :: Energy = %f; diff = %f kcal/mol; \n", step,Ef, delta_E);
            if (fabs(delta_E) < error_tolerance && delta_E!=0) {
                printf("Finished with energy = %f kcal/mol \n", Ef);
                converged=1;
            }
        } else {
            // reject
            for (int n=0;n<3;n++) system.molecules[0].atoms[randatom].pos[n] = tmp_pos[n];
        }

        // check max-steps convergence
        if (step >= step_limit) {
                printf("Finished with energy = %f kcal/mol \n", Ei); 
                converged=1;
        }

    } // end while loop for convergence
    } // end MC style opt
    
    // steepest desent (follow the negative gradient)
    else if (optmode == OPTIMIZE_SD) {
        while (!converged) {
            Ei = stretch_energy(system) + angle_bend_energy(system);
            morse_gradient(system);
            Ef = stretch_energy(system) + angle_bend_energy(system);
            delta_E = Ef - Ei;

            printf("Step %i :: Energy = %f; diff = %f kcal/mol; \n", step,Ef, delta_E);

            if (fabs(delta_E) < error_tolerance && delta_E!=0) {
                 printf("Finished with energy = %f kcal/mol \n", Ef);
                converged=1;
            }

            if (step >= step_limit) {
                printf("Finished with energy = %f kcal/mol \n", Ei);
                converged=1;
            }
            step++;
        }
    }

} // end optimize