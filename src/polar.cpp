#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cmath>

#include "thole_iterative.cpp"

#define OneOverSqrtPi 0.56418958354

using namespace std;

void makeAtomMap(System &system) {
    //int count =0;
    int i,j;
    // delete all elements from the atommap!! (i wasn't doing this before. This was the bug)
    system.atommap.clear();

    vector<int> local = vector<int>(2);
    //int v[2];
    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            local = {i,j};
            system.atommap.push_back(local);

        //printf("system.atommap[%i] = {%i,%i}\n", count, system.atommap[count][0], system.atommap[count][1]);
          //  count++;
        }
    }
    //printf("SIZE OF ATOM MAP: %i\n", (int)system.atommap.size());
    return;
}

void print_matrix(System &system, int N, double **matrix) {
    //system.checkpoint("PRINTING MATRIX");
    int i,j;
    printf("\n");
    for (i=0; i<N; i++) {
        for (j=0; j<N; j++) {
            printf("%.3f ", matrix[i][j]);
        }
        printf("\n");
    }
    printf("\n");

    /* This is for the 1/2 A-matrix, deactivated for now. 
    // NEW ***
    int blocksize=3, inc=0;
    printf("\n");
    for (i=0; i<N; i++) {
        for (j=0; j<blocksize; j++) {
            printf("%.3f ", matrix[i][j]);
        }
        printf("\n");
        inc++;
        if (inc%3==0) blocksize+=3;
    }
    */
}

void zero_out_amatrix(System &system, int N) {
    int i,j;
    // NEW ***
    int blocksize=3, inc=0;
    for (i=0; i<3*N; i++) {
        for (j=0; j<blocksize; j++) {
            system.constants.A_matrix[i][j] = 0;    
        }
        inc++;
        if (inc%3==0) blocksize+=3;
    }
    return;
}

double get_dipole_rrms (System &system) {
    double N, dipole_rrms;
    dipole_rrms = N = 0;
    int i,j;
    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            //if (isfinite(system.molecules[i].atoms[j].dipole_rrms))
            if (system.molecules[i].atoms[j].dipole_rrms != system.molecules[i].atoms[j].dipole_rrms)
               dipole_rrms += system.molecules[i].atoms[j].dipole_rrms;
            N++;
        }
    }

    return dipole_rrms / N;
}

/* for uvt runs, resize the A matrix */
void thole_resize_matrices(System &system) {

    int i, N, dN, oldN;

    //just make the atom map of indices no matter what.
    // for whatever reason things get buggy when I try to
    // minimize calls to this function. It's not expensive anyway.
    makeAtomMap(system);

    /* determine how the number of atoms has changed and realloc matrices */
    oldN = 3*system.last.thole_total_atoms; //will be set to zero if first time called
    system.last.thole_total_atoms = system.constants.total_atoms;
    N = 3*system.last.thole_total_atoms;
    dN = N-oldN;

    //printf("oldN: %i     N: %i     dN: %i\n",oldN,N,dN);

    if(!dN) { return; }

    // NEW ***
    for (i=0; i< oldN; i++) free(system.constants.A_matrix[i]);
    free(system.constants.A_matrix);
    system.constants.A_matrix = (double **) calloc(N, sizeof(double*));
    int blocksize=3, inc=0;
    for (i=0; i<N; i++) {
        system.constants.A_matrix[i] = (double *) malloc(blocksize*sizeof(double));
        inc++;
        if (inc%3==0) blocksize+=3;
    }

     return;
}

/* calculate the dipole field tensor */
void thole_amatrix(System &system) {

    int i, j, ii, jj, N, p, q;
    int w, x, y, z;
    double damp1=0, damp2=0; //, wdamp1=0, wdamp2=0; // v, s; //, distancesp[3], rp;
    double r, r2, ir3, ir5, ir=0;
    const double rcut = system.pbc.cutoff;
    //const double rcut2=rcut*rcut;
    //const double rcut3=rcut2*rcut;
    const double l=system.constants.polar_damp;
    const double l2=l*l;
    const double l3=l2*l;
    double explr; //exp(-l*r)
    const double explrcut = exp(-l*rcut);
    const double MAXVALUE = 1.0e40;
    N = (int)system.constants.total_atoms;
    double rmin = 1.0e40;

    //system.checkpoint("in thole_amatrix() --> zeroing out");
    zero_out_amatrix(system,N);
    //system.checkpoint("done with zero_out_amatrix()");

    ////system.checkpoint("setting diagonals in A");
    /* set the diagonal blocks */
    for(i = 0; i < N; i++) {
        ii = i*3;
        w = system.atommap[i][0];
        x = system.atommap[i][1];

        // NEW ***
        for (p=0; p<3; p++) {
            if (system.molecules[w].atoms[x].polar != 0.0)
                system.constants.A_matrix[ii+p][ii+p] = 1.0/system.molecules[w].atoms[x].polar;
            else
                system.constants.A_matrix[ii+p][ii+p] = MAXVALUE;
        }

    }
    ////system.checkpoint("done setting diagonals in A");

    ////system.checkpoint("starting Tij loop");
    /* calculate each Tij tensor component for each dipole pair */
    for(i = 0; i < (N - 1); i++) {
        ii = i*3;
        w = system.atommap[i][0]; x = system.atommap[i][1];
        for(j = (i + 1);  j < N; j++) {
            jj = j*3;
            y = system.atommap[j][0]; z = system.atommap[j][1];

            //printf("i %i j %i ======= w %i x %i y %i z %i \n",i,j,w,x,y,z);

            double* distances = getDistanceXYZ(system, w,x,y,z);
            r = distances[3];

            if (r<rmin && (system.molecules[w].atoms[x].polar!=0 && system.molecules[y].atoms[z].polar!=0))
                rmin=r; // for ranking, later


            // this on-the-spot distance calculator works, but the new method
            // below does not work, even though everywhere else, it does...
            //rp = system.pairs[w][x][y][z].r;
            //for (int n=0;n<3;n++) distancesp[n] = system.pairs[w][x][y][z].d[n];
            r2 = r*r;

            //printf("r: %f; rp: %f\n", r, rp);

            ////system.checkpoint("got r.");
            //printf("distances: x %f y %f z %f r %f\n", distances[0], distances[1], distances[2], r);

            /* inverse displacements */
            if(r == 0.)
                ir3 = ir5 = MAXVALUE;
            else {
                ir = 1.0/r;
                ir3 = ir*ir*ir;
                ir5 = ir3*ir*ir;
            }

            //evaluate damping factors
                    explr = exp(-l*r);
                    damp1 = 1.0 - explr*(0.5*l2*r2 + l*r + 1.0);
                    damp2 = damp1 - explr*(l3*r2*r/6.0);

            ////system.checkpoint("got damping factors.");

           // //system.checkpoint("buildling tensor.");
            /* build the tensor */
            // NEW *** NEEDED FOR 1/2 MATRIX
            for (p=0; p<3; p++) {
                for (q=0; q<3; q++) {
                       system.constants.A_matrix[jj+p][ii+q] = -3.0*distances[p]*distances[q]*damp2*ir5;
                       // additional diagonal term
                       if (p==q)
                           system.constants.A_matrix[jj+p][ii+q] += damp1*ir3;
                }
            }
        } /* end j */
    } /* end i */

    system.constants.polar_rmin = rmin;
    return;
}

void thole_field(System &system) {
    // wolf thole field
    int i,j,k,l,p;
    const double SMALL_dR = 1e-12;
    double r, rr; //r and 1/r (reciprocal of r)
    const double R = system.pbc.cutoff;
    const double rR = 1./R;
    //used for polar_wolf_alpha (aka polar_wolf_damp)
    const double a = system.constants.polar_wolf_alpha; //, distances[3];
    const double erR=erfc(a*R); //complementary error functions
    const double cutoffterm = (erR*rR*rR + 2.0*a*OneOverSqrtPi*exp(-a*a*R*R)*rR);
    double bigmess=0;


    // first zero-out field vectors
    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            for (p=0; p<3; p++) {
                system.molecules[i].atoms[j].efield[p] = 0;
                system.molecules[i].atoms[j].efield_self[p] = 0;
            }
        }
    }


    for(i=0; i<system.molecules.size(); i++) {
        for(j=0; j<system.molecules[i].atoms.size(); j++) {
            for(k=i+1; k<system.molecules.size(); k++) { // molecules not allowed to self-polarize
            for (l=0; l<system.molecules[k].atoms.size(); l++) {

                if ( system.molecules[i].frozen && system.molecules[k].frozen ) continue; //don't let the MOF polarize itself

                double* distances = getDistanceXYZ(system, i,j,k,l);
                r = distances[3];
                //r = system.pairs[i][j][k][l].r;
                //for (int n=0;n<3;n++) distances[n] = system.pairs[i][j][k][l].d[n];

                if((r - SMALL_dR  < system.pbc.cutoff) && (r != 0.)) {
                    rr = 1./r;

                    if ( a != 0 )
                        bigmess=(erfc(a*r)*rr*rr+2.0*a*OneOverSqrtPi*exp(-a*a*r*r)*rr);

                    for ( p=0; p<3; p++ ) {
                        //see JCP 124 (234104)
                        if ( a == 0 ) {

                            // the commented-out charge=0 check here doesn't save time really.
                            //if (system.molecules[k].atoms[l].C != 0) {
                                system.molecules[i].atoms[j].efield[p] +=
                                (system.molecules[k].atoms[l].C)*
                                (rr*rr-rR*rR)*distances[p]*rr;
                            //}
                            //if (system.molecules[i].atoms[j].C != 0) {
                                system.molecules[k].atoms[l].efield[p] -=
                                (system.molecules[i].atoms[j].C )*
                                (rr*rr-rR*rR)*distances[p]*rr;
                            //}

                        } else {
                            //if (system.molecules[k].atoms[l].C != 0) {
                                system.molecules[i].atoms[j].efield[p] +=
                                (system.molecules[k].atoms[l].C )*
                                (bigmess-cutoffterm)*distances[p]*rr;
                            //}
                            //if (system.molecules[i].atoms[j].C != 0) {
                                system.molecules[k].atoms[l].efield[p] -=
                                (system.molecules[i].atoms[j].C )*
                                (bigmess-cutoffterm)*distances[p]*rr;
                            //}
                         }
                      //      printf("efield[%i]: %f\n", p,system.molecules[i].atoms[j].efield[p]);

                    } // end p

                } //cutoff
            } // end l
            }  // end k
        } // end j
    } // end i

    /*
    printf("THOLE ELECTRIC FIELD: \n");
    for (int i=0; i<system.molecules.size(); i++)
        for (int j=0; j<system.molecules[i].atoms.size(); j++)
            printf("ij efield: %f %f %f\n", system.molecules[i].atoms[j].efield[0], system.molecules[i].atoms[j].efield[1], system.molecules[i].atoms[j].efield[2]);
    printf("=======================\n");
    */
    return;


} // end thole_field()

void thole_field_nopbc(System &system) {
    int p, i, j, k,l;
    double r;
    const double SMALL_dR = 1e-12; //, distances[3];

    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            for (k=i+1; k<system.molecules.size(); k++) {
                for (l=0; l<system.molecules[k].atoms.size(); l++) {
                    if (system.molecules[i].frozen && system.molecules[k].frozen) continue;
                    double* distances = getDistanceXYZ(system,i,j,k,l);
                    r = distances[3];
                    //r = system.pairs[i][j][k][l].r;
                    //for (int n=0;n<3;n++) distances[n] = system.pairs[i][j][k][l].d[n];

                    if ( (r-SMALL_dR < system.pbc.cutoff) && (r != 0.)) {
                        for (p=0; p<3; p++) {
                            system.molecules[i].atoms[j].efield[p] += system.molecules[k].atoms[l].C * distances[p]/(r*r*r);
                            system.molecules[k].atoms[l].efield[p] -= system.molecules[i].atoms[j].C * distances[p]/(r*r*r);
                        }
                    }
                }
            }
        }
    }
} // end thole_field_nopbc

// =========================== POLAR POTENTIAL ========================
double polarization(System &system) {

    // POLAR ITERATIVE METHOD FROM THOLE/APPLEQUIST IS WHAT I USE.
    // THERE ARE OTHERS, E.G. MATRIX INVERSION OR FULL EWALD
    // MPMC CAN DO THOSE TOO, BUT WE ALMOST ALWAYS USE ITERATIVE.
    double potential; int i,j,num_iterations;

    // 00) RESIZE THOLE A MATRIX IF NEEDED
    if (system.constants.ensemble == ENSEMBLE_UVT) { // uVT is the only ensemble that changes N
        thole_resize_matrices(system);
    }

    // 0) MAKE THOLE A MATRIX
    thole_amatrix(system); // ***this function also makes the i,j -> single-index atommap.

    // 1) CALCULATE ELECTRIC FIELD AT EACH SITE
    if (system.constants.mc_pbc)
        thole_field(system);
    else
        thole_field_nopbc(system); // maybe in wrong place? doubt it. 4-13-17


    // 2) DO DIPOLE ITERATIONS
    num_iterations = thole_iterative(system);
    system.stats.polar_iterations = (double)num_iterations;
    system.constants.dipole_rrms = get_dipole_rrms(system);


    // 3) CALCULATE POLARIZATION ENERGY 1/2 mu*E
    potential=0;
    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            potential +=
                dddotprod(system.molecules[i].atoms[j].dip, system.molecules[i].atoms[j].efield);

            if (system.constants.polar_palmo) {
                potential += dddotprod(system.molecules[i].atoms[j].dip, system.molecules[i].atoms[j].efield_induced_change);
            }
        }

    }

    potential *= -0.5;
    return potential;

} // end polarization() function



void polarization_force(System &system) {
    // gets force on atoms due to dipoles calculated before (via iterative method)
    // TODO

    int i,j,n;
    double q;

    // using UMS p. 423
    // see also green notebook.
    // F_polar = q*(-u/alpha + E)
    // So, we need to converge on the dipoles first, just like for potential calc.
    // there is no uVT for MD, so no need to re-size the A matrix.
    // this is very much a test and not suitable for publishable use.

    thole_amatrix(system); // fill in the A-matrix
    thole_field(system); // calculate electric field at each atom (assume PBC)
    int num_iterations = thole_iterative(system); // iteratively solve the dipoles
       system.stats.polar_iterations = (double)num_iterations;
          system.constants.dipole_rrms = get_dipole_rrms(system);
    // ready for forces.
    // it's a many-body potential, not pair-potential, so at this point forces are intrinsic to atoms.
    for (i=0; i<system.molecules.size(); i++) {
        for (j=0; j<system.molecules[i].atoms.size(); j++) {
            if (system.molecules[i].atoms[j].frozen) continue; // skip frozens.
            if (system.molecules[i].atoms[j].polar == 0.) continue; // skip non-polarizable sites
            if (system.molecules[i].atoms[j].C == 0) continue; // skip 0-force
            q = system.molecules[i].atoms[j].C;

            for (n=0;n<3;n++) {
                //printf("force before[%i] = %e\n", n, system.molecules[i].atoms[j].force[n]);
                //printf("gamma: %f; dip: %f; q: %f; polar: %f; efield: %f; efield_self: %f\n", system.constants.polar_gamma, system.molecules[i].atoms[j].dip[n], system.molecules[i].atoms[j].C, system.molecules[i].atoms[j].polar, system.molecules[i].atoms[j].efield[n], system.molecules[i].atoms[j].efield_self[n]);
                system.molecules[i].atoms[j].force[n] += q*((-system.constants.polar_gamma*system.molecules[i].atoms[j].dip[n]  / system.molecules[i].atoms[j].polar)                               + ((system.molecules[i].atoms[j].efield[n] + system.molecules[i].atoms[j].efield_self[n])));
                //printf("force after[%i] = %e\n", n, system.molecules[i].atoms[j].force[n]);
            }
        } // end loop j atoms in i
    } // end loop i molecules
}
