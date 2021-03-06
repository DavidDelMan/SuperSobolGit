#include "SobolIndices.h"
#include <fstream>

/* Ctor
 * Input:
 *
 * model_ = Type-valued function of a vector of Types.  First arg is
 *   vector of parameters to be drawn randomly.  Second arg is vector
 *   of fixed constants, like strike & interest rate.
 * constants_ = vector of constants for model, like strike price
 * indices_ = set of parameter indices to compute SIs for
 * dim_ = number of parameters in model
 * N_MC_ = number of Monte Carlo runs
 * CoV_ = defaulted to 1.0 to construct object without specifying CoV,
 *   used for CoV script.
 */
SobolIndices::
SobolIndices(Type (*model_)(const std::vector<Type>&,
			    const std::vector<Type>&),
	     const std::vector<Type> &constants_,
	     const std::set<int> &indices_,
	     const std::vector<std::vector<Type> >
	     &initialDistroParams_,
	     int dim_,
	     unsigned int N_MC_,
	     Type CoV_)
{
  model = model_;
  constants = constants_;
  indices = indices_;
  distroParams = initialDistroParams_;
  dim = dim_;
  N_MC = N_MC_;
  CoV = CoV_;

  /* initialize SIs */
  lowerIndex = 0;
  totalIndex = 0;
  modelVariance = 0;
  modelMean = 0;

  /* allocate memory for model arg vectors */
  x1.resize(dim);
  x2.resize(dim);
  arg1.resize(dim);
  arg2.resize(dim);

  /* construct halton (RASRAP) & InverseTransformation objects */
  randomNumberGenerator = new halton();
  invTrans = new InverseTransformation();

  /* init RNG: length of Halton vector, random start, random permute */
  randomNumberGenerator->init(2*dim,true,true);
}

/* Displays member variables of the SobolIndices class */
void SobolIndices::DisplayMembers()
{
  std::cout << "Members of SobolIndices: \n\n";
  std::cout << "dim: " << dim << "\n";
  std::cout << "N_MC: " << N_MC << "\n";
  std::cout << "CoV: " << CoV << "\n";
  std::cout << "lowerIndex: " << lowerIndex << "\n";
  std::cout << "totalIndex: " << totalIndex << "\n";
  std::cout << "modelVariance: " << modelVariance << "\n";
  std::cout << "modelMean: " << modelMean << "\n";
  std::cout << "indices: \n";
  DisplaySet(indices);
  std::cout << "distroParams: \n";
  DisplayVector(distroParams);

  // std::cout << "randomNumbers: \n";
  // DisplayVector(randomNumbers);
  // std::cout << "x: \n";
  // DisplayVector(x);
  // std::cout << "arg1: \n";
  // DisplayVector(arg1);
  // std::cout << "arg2: \n";
  // DisplayVector(arg2);
  std::cout << "\n";
}

/* Computes the upper and lower Sobol' indices.  Assigns the member
 * variables lowerIndex, totalIndex, modelVariance and modelMean.
 *
 * Overloading previous function to allow different indices than those
 * passed into ctor, as need in CoV routine.
 *
 * Input:
 *   uncertainties = vector of parameter variances to use
 *   indices - set of parameters to compute sensitivity index for,
 *             defaulted to empty in header
 */
Type SobolIndices::
ComputeSensitivityIndices(const std::vector<Type> &uncertainties,
			  const std::set<int> &indices_)
{
  // std::cout << "Computing SIs, CoV \n";

  /* MC accumulators */
  Type f0_sum = 0, Dy_sum = 0, DT_sum = 0, D_sum = 0;

  /* model evaluations */
  Type f, f2, model1, model2;

  for (unsigned int i = 0; i < N_MC; ++i)
    {
      /* generate 2*dim random numbers */
      randomNumberGenerator->genHalton();

      /* transform each random number to its distro. */
      TransformToModelDomain(uncertainties);

      /* assign xformed random numbers to proper model arg vectors */
      if (indices_.empty())
	{
	  AssignModelArguments(indices);
	}
      else
	{
	  AssignModelArguments(indices_);
	}

      // DisplayVector(x1);
      // DisplayVector(x2);
      // DisplayVector(arg1);
      // DisplayVector(arg2);

      /* MC accumulations */
      f = model(x1,constants);
      f2 = model(x2,constants);
      model1 = model(arg1,constants);
      model2 = model(arg2,constants);

      // std::cout << "f = " << f << "\n";
      // std::cout << "f2 = " << f2 << "\n";
      // std::cout << "model1 = " << model1 << "\n";
      // std::cout << "model2 = " << model2 << "\n";


      f0_sum += f;
      D_sum += f*f;
      Dy_sum += f*(model1 - f2);
      DT_sum += pow((f - model2), 2.0);
    }

  /* compute sensitivity indices */
  modelMean = f0_sum/N_MC;
  modelVariance = D_sum/N_MC  - modelMean*modelMean;

  Type Dy = Dy_sum/N_MC;
  Type DT = DT_sum/N_MC;

  // std::cout << "Dy = " << Dy << "\n";
  // std::cout << "DT = " << DT << "\n";

  //  /* normalized */
  // lowerIndex = Dy/modelVariance;
  // totalIndex = DT/(2.0*modelVariance);

  /* non-normalized */
  lowerIndex = Dy;
  totalIndex = DT/2.0;

  return totalIndex;

}

/* Function AssignModelArguments fills the two vectors that will be 
 * passed to the model for evaluation in computing the Sobol' indices.
 */
void SobolIndices::
AssignModelArguments(const std::set<int>& indices_)
{
  for (int j = 1; j <= dim; ++j)
    {
      /* check if j is in the index set to compute SIs for */
      bool inIndexSet = indices_.count(j);

      if (inIndexSet)
	{
	  arg1[j-1] = x1[j-1];
	  arg2[j-1] = x2[j-1];
	}
      else
	{
	  arg1[j-1] = x2[j-1];
	  arg2[j-1] = x1[j-1];
	}
    }
}

/* Function TransformToModelDomain changes the elements of
 * x1 and x2 member vectors (the vectors passed to the model in
 * computation of the SIs) to fit in the model domain.  I.e., it takes
 * each arg vector, which is composed of Unif(0,1) random numbers, and
 * transforms each component to its respective distro.
 *
 * Input:
 *    (optional) uncertainties = vector of parameter uncertainties,
 *        defaulted to empty in header.  This is for use with Super
 *        Sobol index computation.
 */
void SobolIndices::
TransformToModelDomain(const std::vector<Type> &uncertainties)
{
  for (int j = 0; j < dim; ++j)
    {
      /* get the RNs generated from ComputeSensitivityIndices fn */
      x1[j] = randomNumberGenerator->get_rnd(j+1);
      x2[j] = randomNumberGenerator->get_rnd(j+1+dim);

      // x1[j] = RNG->genrand64_real3();
      // x2[j] = RNG->genrand64_real3();

      /* true if "j" is in ORIGINAL index set */
      bool inIndexSet = indices.count(j+1);

      Type mean = distroParams[j][0], var;

      /* If parameter uncertainty not changed, leave as initial. Ow
       * change to new uncertainty. */
      if (uncertainties.empty())
	{
	  var = distroParams[j][1];
	}
      else
	{
	  var = uncertainties[j];
	}

      /* change variance of parameter of interest by CoV.
       // * Also need to check the the index set being passed in is the
       // * one of the original index set, since also passing in the
       // * complement of that set for CoV.  Don't want to change the
       // * CoV of those.
       */
      // if (inIndexSet)
      // 	{
      // 	  var = pow(distroParams[j][0]*CoV, 2.0);
      // 	}

      x1[j] = invTrans->Normal(x1[j], mean, var);
      x2[j] = invTrans->Normal(x2[j], mean, var);
    }

  // /* For Vasicek, drew log a, log b, log sigma, so convert back to
  //  * original params a,b,sigma */
  // x1[0] = exp(x1[0]);  // convert log a -> a
  // x2[0] = exp(x2[0]);  // convert log a -> a
  // x1[1] = exp(x1[1]);  // convert log b -> b
  // x2[1] = exp(x2[1]);  // convert log b -> b
  // x1[2] = exp(x1[2]);  // convert log sigma -> sigma
  // x2[2] = exp(x2[2]);  // convert log sigma -> sigma
}

/* Computes the indices for the range of CoVs in the CoV_ vector.
 * The resulting indices are stored in a 2D vector:
 *     first row = total index of origianl set,
 *     second row = lower index of complement set,
 *     third row = model variance.
 * These indices are written to a file for plotting.
 *
 * Input:
 *     CoV_Vector = vector of CoVs to use in plotting
 *     filename = name of file for plotting
 */
std::vector<std::vector<Type> > SobolIndices::
PlotCoV(const std::vector<Type> &CoV_Vector,
	std::string &filename)
{
  std::cout << "PlotCoV \n";

  // /* allocate Sobol index vectors */
  // size_t num_CoV = CoV_Vector.size();
  // std::vector<std::vector<Type> > 
  //   results(3, std::vector<Type>(num_CoV));

  // /* construct set containing complement of current param index */
  // std::set<int> complement_indices;
  // for (int i = 0; i < dim; ++i)
  //   {
  //     /* true if "i" is in index set */
  //     bool inIndexSet = indices.count(i+1);

  //     /* add index to complement set if not in original index set */
  //     if (!inIndexSet)
  // 	{
  // 	  complement_indices.insert(i+1);
  // 	}
  //   }

  // /* compute sensitivity indices */
  // for (int i = 0; i < num_CoV; ++i)
  //   {
  //     /* change the class' CoV to the current one in the vector */
  //     CoV = CoV_Vector[i];

  //     /* compute the sobol indices for original index using this CoV */
  //     ComputeSensitivityIndices();

  //     /* store total index of original set in index vector */
  //     results[0][i] = totalIndex;

  //     /* compute sobol indices for complement set */
  //     ComputeSensitivityIndices(complement_indices);

  //     /* store lower index of complement set in index vector */
  //     results[1][i] = lowerIndex;

  //     /* store model variance */
  //     results[2][i] = modelVariance;
  //   }

  // /* open plotting file and write data to it */
  // std::ofstream plotFile(filename.c_str());
  // if (plotFile.is_open())
  //   {
  //     for (int i = 0; i < num_CoV; ++i)
  // 	{
  // 	  plotFile << CoV_Vector[i] << " " << results[0][i] << " " 
  // 		   << results[1][i] << " " << " " 
  // 		   << results[2][i] << "\n";
  // 	}
  //   }
  // else
  //   {
  //     std::cout << "unable to open plot file \n";
  //   }

  // /* close plotting file */
  // plotFile.close();

  // /* return Sobol indices */
  // return results;
}

void SobolIndices::DisplayVector(const std::vector<Type>& vec)
{
  for (auto& i : vec)
    {
      std::cout << i << " ";
    }
  std::cout << "\n";
}

void SobolIndices::DisplaySet(const std::set<int>& s)
{
  for (auto i : s)
    {
      std::cout << i << " ";
    }
  std::cout << "\n\n";
}

void SobolIndices::
DisplayVector(const std::vector<std::vector<Type> >& vec)
{
  for (const auto& i : vec)
    {
      for (const auto& j : i)
	{
	  std::cout << j << " ";
	}
      std::cout << "\n";
    }
  std::cout << "\n";
}

// /* Allows the parameters distributions to be changed from those passed
//  * into the ctor.  Written for Super Sobol indices routines. */
// SobolIndices::
// void SetDistroParams(const std::vector<std::vector<Type> >&
// 		     distroParams_)
// {
//   distroParams = distroParams_;
// }
