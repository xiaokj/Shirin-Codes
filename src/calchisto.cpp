// TODO: BtagEffGiver has issue reading the 2d hist. 
// TODO: Create and Write() Boson and special angles hist.
// TODO: ADD AXIS to stack plotter
// TODO: CMS and MET need weights, lest crash
#include <ROOT/RCsvDS.hxx>//#include <ROOT/RDataFrame.hxx>
#include <TLorentzVector.h>
#include <TRandom3.h>// used Gaussian once
#include <TSystem.h>// for safer RCsvDS via gSystem
#include <TChain.h>

#include "calchisto.hpp"
#include "eval_complex.hpp"

using doubles = ROOT::VecOps::RVec<double>;
using  floats = ROOT::VecOps::RVec<float>;
using    ints = ROOT::VecOps::RVec<int>;
using   bools = ROOT::VecOps::RVec<bool>;
using   chars = ROOT::VecOps::RVec<UChar_t>;// aka 1 byte ints
using strings = ROOT::VecOps::RVec<std::string>;

namespace{
  constexpr int   debug = 1;
  constexpr float ENDCAP_ETA_MIN = 1.566f;
  constexpr float BARREL_ETA_MAX = 1.4442f;
//constexpr int   EL_MAX_NUM   = 1;
  constexpr float EL__PT_MIN   = 45.f;//{15}//min 12, AP 45,
//constexpr float EL_LPT_MIN   = 35.f;// Leading
  constexpr float EL_ETA_MAX   = 2.5f;
  constexpr int   EL_LOOSE_ID  = 1;
  constexpr int   EL_TIGHT_ID  = 4;

//constexpr int   MU_MAX_NUM   = 1;
  constexpr float MU__PT_MIN   = 40.f;//min 33, AP 40,
//constexpr float MU_LPT_MIN   = 26.f;// Leading
  constexpr float MU_ETA_MAX   = 2.4f;
  constexpr float MU_LOOSE_ISO = .15f;
  constexpr float MU_TIGHT_ISO = .25f;

//constexpr float MET__PT_MIN  = 40.f;
  constexpr float MET_EL_PT    = 80.f;
  constexpr float MET_MU_PT    = 40.f;

  constexpr float   Z_MASS     =  91.1876f;
  constexpr float   Z_MASS_CUT =  20.f;
  constexpr float   W_MASS     =  80.385f;
  constexpr float   W_MASS_CUT =  20.f;
  constexpr float TOP_MASS     = 172.5f;
//constexpr float TOP_MASS_CUT =  20.f;

constexpr float    JET_ETA_MAX =  4.7f;
constexpr float    JET__PT_MIN = 30.f;
constexpr float        JET_ISO =   .4f;
constexpr unsigned    JETS_MIN = 4;
constexpr unsigned    JETS_MAX = 6;

constexpr float   BJET_ETA_MAX = 2.4f;
constexpr double BTAG_DISC_MIN =  .8838;
constexpr unsigned   BJETS_MIN = 1;
constexpr unsigned   BJETS_MAX = 3;

//constexpr float DELTA___R_ZL   = 1.6f;
//constexpr float DELTA_PHI_ZW   = 2;
//constexpr float DELTA_PHI_ZMET = 2;

constexpr float RconeBy2 =  .2f;

constexpr float    TZQ_W =  .0128f;
constexpr float WWLNQQ_W = 2.1740f;
constexpr float WZLNQQ_W =  .2335f;
constexpr float  TTZQQ_W =  .0237f;
constexpr float ZZLLQQ_W =  .0485f;

// This Pi is more accurate than binary256; good for eternity
template <typename T> constexpr T  PI = T(3.14159265358979323846264338327950288419716939937510582097494459230781640628620899);
template <typename T> constexpr T TPI = PI<T> * 2;

auto met_pt_cut(const channel ch){
	float lower_bound;
	switch(ch){
		case elnu:{lower_bound = MET_EL_PT;break;}
		case munu:{lower_bound = MET_MU_PT;break;}
//		default  :throw std::invalid_argument(
//			"Unimplemented ch (met_pt_cut)");
	}
	return [=](const float&  met_lep_pt_sel)->bool
	   {return lower_bound < met_lep_pt_sel;};
}
auto lep_sel(    const channel ch){
      return [=](const  bools& isPFs,
                 const floats& pts,
                 const floats& etas,
                 const   ints& elids,
                 const  bools& muids,
                 const floats& isos){
		const auto abs_etas = abs(etas);
		switch(ch){
			case elnu:return (isPFs && pts >  EL__PT_MIN
			                 && ((abs_etas <  EL_ETA_MAX
			                 &&   abs_etas >  ENDCAP_ETA_MIN)
			                 ||  (abs_etas <  BARREL_ETA_MAX))
			                 &&      elids >= EL_LOOSE_ID);
			case munu:return (    muids    && isPFs
			                 &&   pts      >  MU__PT_MIN
			                 &&   abs_etas <  MU_ETA_MAX
			                 &&   isos     <= MU_LOOSE_ISO);// TODO: WEIRD
//			default  :throw std::invalid_argument(
//				"Unimplemented ch (lep_sel)");
		}
	};
}
auto lep_tight_cut(const channel ch){
        return [=](const   ints& mask,
                   const   ints& elids,
                   const floats& isos){
		bool result;
		      if(ch==elnu){
			ints   temp = elids[mask];
			result = temp.size() == 1;// Choosing 1 Tight Lepton
			result = result && temp[0] >= EL_TIGHT_ID;
		}else if(ch==munu){
			floats temp = isos[mask];
			result = temp.size() == 1;// TODO: WEIRD ISO
			result = result && temp[0] <= MU_TIGHT_ISO;
		}else{throw std::invalid_argument(
			"Unimplemented ch (lep_tight_cut)");}
		return result;
	};
}
template <typename T>
[[gnu::const]] auto fastPrincipalRangeReductor(const T diff_phi){
	// This function just reduces input from [-2pi,+2pi] to [-pi,+pi]
	// Domain correctness is the user's responsibility(eg.subtract phis)
	// A more general function is just one fmod function away
	if(    diff_phi >  PI<T>) return diff_phi - TPI<T>;
	if(    diff_phi < -PI<T>) return diff_phi + TPI<T>;
	return diff_phi;
}
template <typename T> inline constexpr auto delta_phi(const T dp)
	{return fastPrincipalRangeReductor(dp);}

[[gnu::const]] auto deltaR(
	const float eta1,const float phi1,
	const float eta2,const float phi2)
	{return std::hypot(eta1-eta2,delta_phi(phi1-phi2));}//hypot from geometry

float transverse_w_mass(const float lep__pt,
                        const float lep_phi,
                        const float met__pt,
                        const float met_phi){
	return static_cast<float>(
		      std::sqrt(2*lep__pt*met__pt
		*(1-cos(delta_phi(lep_phi-met_phi)))));
}
auto lep_nu_invmass(const float& lep_pt    ,
                    const float& lep_eta   ,
                    const float& lep_phi   ,
                    const float& lep_mass  ,
                    const float& cal_metpt ,
                    const float& cal_metphi){//,
                  //const float& cal_metEt ){// cal_metEt is unused
	// this function computes the invariant mass of charged lepton
	// and neutrino system, in order to calculate the W mass later on.
	TLorentzVector lep,neu;
	lep.SetPtEtaPhiM(   lep_pt,lep_eta,   lep_phi,lep_mass);
	neu.SetPtEtaPhiM(cal_metpt,lep_eta,cal_metphi,0.);
	return static_cast<float>((lep+neu).M());
}
/*
auto w_mass_cut(const float& w_mass)
	{return std::abs(w_mass-W_MASS)<W_MASS_CUT;};
*/
auto z_mass_cut(const float& z_mass)
	{return std::abs(z_mass-Z_MASS)<Z_MASS_CUT;}

// TODO: top_mass_cut belongs here
template<typename T,typename U>
[[gnu::const]] bool all_equal(const T& t, const U& u){return t == u;}
template<typename T,typename U,typename... Types>
[[gnu::const]] bool all_equal(const T& t, const U& u, Types const&... args)
	{return t == u && all_equal(u, args...);}
auto jet_lep_min_deltaR(const floats& jet_etas,
                        const floats& jet_phis,
                        const float & lep_eta ,
                        const float & lep_phi){
	if(!all_equal(jet_etas.size(),jet_phis.size()))
		throw std::logic_error(
		      "Collections must be the same size (jet-lep dR)");
	if(jet_phis.empty())
		throw std::logic_error(
		      "Collections must not be empty for (jet-lep dR)");
	if(debug>0) std::cout<<"in jet_lep_min_dR"<<std::endl;
	floats min_dRs; min_dRs.reserve(jet_etas.size());
	std::transform(
		jet_etas.cbegin(),
		jet_etas.  cend(),
		jet_phis.cbegin(),
		std::back_inserter(min_dRs),
		[=](float jet_eta, float jet_phi){
			return deltaR(jet_eta,jet_phi,lep_eta,lep_phi);});
	return min_dRs;
}
auto tight_jet_id(const floats& jet_lep_min_dRs,
                  const floats& pts,
                  const floats& etas,
                  const   ints& ids){
	if(debug>0) std::cout<<"tight_jet_id"<<std::endl;
	return pts>JET__PT_MIN&&abs(etas)<JET_ETA_MAX&&jet_lep_min_dRs>JET_ISO&&ids>=2;
}
auto jetCutter(const unsigned jmin, const unsigned jmax){
	if(debug>3) std::cout<<"jet cut "<< jmin <<" "<< jmax <<std::endl;
	return[=](const ints& jetmask){
		const auto nj = std:: count_if(
			jetmask.cbegin(),
			jetmask.  cend(),
			[](int i){return i;});// 0 is false
		return jmin <= nj && nj <= jmax;
	};
}
auto jets_gen_select(const floats& gen, const floats& jet){
	if(debug>0) std::cout<<"gen select"<<std::endl;
// select jets that have generated level info; used @ JEC
// use this function ONLY for Monte Carlo, not CMS / MET
	if(gen.size() < jet.size()){// GenJet often shorter than Jet
	    floats good_jets =  jet;//             deep copy
	           good_jets.resize(gen.size());// discard tail only
	    return good_jets;
	}
	    return      jet;
}
template<typename T>// allow us to return w/o knowing data type
auto retVar(const T& v){return[&](){return v;};}

auto jet_smear_pt_resol(
	const floats& pt,const floats& eta,const float& rho){
	if(debug>0) std::cout<<"jet smear pt resol"<<std::endl;
	floats  resol(pt.size());
	if(!all_equal(pt.size(),eta.size())) throw std::logic_error(
		"Collections must be the same size (jet_smear_pt_resol)");
	if(pt.empty()) throw std::logic_error(
		"Collections must not be empty in  (jet_smear_pt_resol)");
	const char csvname[] = "Fall17_V3_MC_PtResolution_AK4PFchs.txt";
	if(gSystem->AccessPathName(csvname)) throw std::runtime_error(// FileNotFound
		"RCsvDS would hang on access failure (jet_smear_pt_resol)");
	auto csvdf = ROOT::RDF::MakeCsvDataFrame(csvname)
	.Define("rho",retVar(static_cast<double>(rho)))// retVar used b/c external rho
	.Filter("rhoMin < rho && rho < rhoMax")
	;
	if("std::string"==csvdf.GetColumnType("rhoMin"))throw std::logic_error(
		"RCsvDS deduced rhoMin as  string; no spaces anywhere in csv file pls");
	if(     "double"!=csvdf.GetColumnType("rhoMin"))throw std::logic_error(
		"RCsvDS deduced rhoMin as integer; edit file, change 0 to 0. for 1st row");
	for(size_t i=0; i < pt.size() ;++i){// this loop is necessary
		resol[i] = static_cast<float>( csvdf
		.Define("pt" ,retVar(static_cast<double>( pt[i])))
		.Define("eta",retVar(static_cast<double>(eta[i])))
		.Filter(" ptMin < pt  &&  pt <  ptMax")
		.Filter("etaMin < eta && eta < etaMax")
		.Define("signAsq",[](double a){return a*std::abs(a);},{"a"})
		.Define("ptSq"   ,"  pt * pt")
		.Define("ptPow"  ,[](double p, double d){return std::pow(p,d);},
		                 {  "pt","d"})
		.Define("pSq"    ,"signAsq/(pt*pt) + b*b * ptPow + c*c")
		.Define("partial",[](double x){return std::sqrt(x);},{"pSq"})
		.Sum(   "partial").GetValue());// total of partials is answer
	}
	return resol;
}
auto jet_smear_Sjer(const floats& eta){
	if(debug>0) std::cout<<"jet smear sjer"<<std::endl;
        floats Sjer(              eta.size());
	const char csvname[] = "Fall17_V3_MC_SF_AK4PF.txt";
	if(gSystem->AccessPathName(csvname)) throw std::runtime_error(
		"RCsvDS would hang on access failure (jet_smear_Sjer)");
	auto csvdf = ROOT::RDF::MakeCsvDataFrame(csvname)
	;// next loop is necessary
	if("std::string"==csvdf.GetColumnType("etaMax"))throw std::logic_error(
		"RCsvDS deduced etaMax as  string; no spaces anywhere in csv file pls");
	for(size_t i=0; i < eta.size() ;++i)
		Sjer[i] = static_cast<float>( csvdf// TODO: add sf up and 
		.Define("eta" ,retVar(static_cast<double>(eta[i])))// down
		.Filter("etaMin < eta && eta < etaMax")// for uncertainty
		.Sum(   "centralSF").GetValue() );// sum all filtered
	return Sjer;
}
[[gnu::const]] auto ramp(const float Sjer){
	if(Sjer > 0.f) return Sjer;
	else           return  0.f;
}
auto delta_R_jet_smear(const floats& pt,
                       const floats& gen_pt,
                       const floats& resol,
                       const floats& Sjer,
                       const floats& deltaR){
	if(debug>0) std::cout<<"delta r jet smear"<<std::endl;
	if(!all_equal(pt.size(),resol.size(),Sjer.size()))
		throw std::logic_error("Collections must be the same size in deltaR_Jsmear");
	if(gen_pt.size() < pt.size())
		throw std::logic_error("gen_pt shorter than pt");
	if(pt.empty())
		throw std::logic_error("Collections must not be empty in deltaR_Jsmear");
	if(pt.size() > deltaR.size())
		throw std::logic_error("deltaR in Delta_R_jet_smear lacks sufficient data");
	const size_t        size = pt.size();
	floats cjers(       size ,0.f);// correction factor
	for(size_t i=0; i < size ;++i){
		if(deltaR[i] < RconeBy2
		&& std::abs( pt[i]-gen_pt[i]) < 3*resol[i]*pt[i]){
			cjers[i] += (1+(1+Sjer[i])
			     * (( pt[i]-gen_pt[i])/pt[i]));
		}
		else{// needs TRandom3 library// TODO: why -ve?
			float Normdist = static_cast<float>(gRandom->Gaus(0,Sjer[i]));
			float  max_val = Sjer[i] * Sjer[i] - 1;
			cjers[i] += (1+Normdist*std::sqrt(ramp(max_val)));
		}
	}
	return cjers;
}
auto      is_bjet_id(const floats& etas,const floats& btags){// added jec_eta
   ints   is_bjet_id = BTAG_DISC_MIN < btags;// A MASK!
          is_bjet_id.resize(etas.size(),0);// discard tail or pad zeroes
   return abs(etas) < BJET_ETA_MAX && is_bjet_id;// now same size,no more error
}
auto      no_bjet_id(const floats& etas){
   return abs(etas) < BJET_ETA_MAX;}

auto      is_bjet_numer(const ints& id,const ints& is_bjet){
   ints   is_bjet_numer = id == 5;
          is_bjet_numer.resize(is_bjet.size(),0);// discard tail or pad zeroes
   return is_bjet_numer;
}
auto      no_bjet_numer(const ints& id,const ints& is_bjet){
// using bjets which has satisfied is btag conditions
	auto aid = abs(id);
	aid = (( aid >  0  && aid <= 4)
	      || aid == 21 || aid != 5);
	aid.resize(is_bjet.size(),0);// discard tail or pad zeroes
	return aid;
}
constexpr auto& is_bjet_denom = is_bjet_numer;// same functions, just 
constexpr auto& no_bjet_denom = no_bjet_numer;// different input mask
auto btagCSVv2(const bool    check_CSVv2){
    return [=](const floats& btag,
               const floats& pt,
               const floats& eta){
	bool ignore = !check_CSVv2;// magic btag checker; heavily reused
	strings formulae(pt.size() ,"0");// vector of "0"
	floats   results(pt.size());
	if(!all_equal(pt.size(),eta.size())) throw std::logic_error(
		"Collections must be the same size in btagCSVv2");
	if(pt.empty())	throw std::logic_error(
		"Collections must not be empty in btagCSVv2");
	if(btag.size() < pt.size()) throw std::logic_error(
		"insufficient btagCSVv2");
	const char csvname[] = "CSVv2_94XSF_V2_B_F.csv";
	if(gSystem->AccessPathName(csvname)) throw std::runtime_error(
		"RCsvDS would hang on access failure (btagCSVv2)");
	auto csvdf = ROOT::RDF::MakeCsvDataFrame(csvname)
	.Filter("measureType==\"comb\"&&sysType==\"central\"&&jetFlav==0")
	// TODO: jet flav 5 or 0?
	.Define("ignore",retVar(ignore))// if true ignore btag and CSVv2
	.Define("bdm"   ,retVar(BTAG_DISC_MIN))// the only double we use
	.Filter("ignore || bdm <= CSVv2")// checks against CSVv2 or not
	;
	if("std::string"==csvdf.GetColumnType("CSVmin"))throw std::logic_error(
		"RCsvDS deduced CSVmin as  string; no spaces anywhere in csv file pls");
	if(     "double"!=csvdf.GetColumnType("CSVmin"))throw std::logic_error(
"RCsvDS deduced CSVmin as integer; edit file, change 0,1 to 0.,1. for 1st row");
	// TODO: uncomment the next two lines if we need CSVv2 as doubles
	// All of CSVv2 are integer and the code ignores this discrepancy
//	if(     "double"!=csvdf.GetColumnType("CSVv2" ))throw std::logic_error(
//		"RCsvDS deduced CSVv2  as integer; edit file, change 0 to 0. for 1st row");
	for(size_t i=0; i < pt.size() ;++i){// this loop is necessary
		std::string tmpFormula = csvdf
		.Define("btag",retVar(static_cast<double>(btag[i])))
		.Define("pt"  ,retVar(static_cast<double>(  pt[i])))
		.Define("eta" ,retVar(static_cast<double>( eta[i])))
		.Filter("ignore || (CSVmin < btag && btag < CSVmax)")
		.Filter(" ptMin < pt  && pt  <  ptMax")
		.Filter("etaMin < eta && eta < etaMax")
		.Filter([](std::string y)// this filter is only a safety check
			{return y.find("x")!=std::string::npos;},{"formula"})
		.Range(1)// get FIRST formula
		.Take<std::string>("formula").GetValue()[0];// Take gives RVec thus [0]
		// now guaranteed one good formula with x in it
		// Time to replace all x with pt ourselves~~ No need boost::replace_all
		size_t pos  = tmpFormula.find("x");
		std::string ptstr = std::to_string(pt[i]);
		while( pos != std::string::npos){
		   tmpFormula.replace(pos,1,ptstr);// 1 is "x".size()
		       pos  = tmpFormula.find("x",pos + ptstr.size());
		}
		formulae[i] = tmpFormula;
	}
	for(size_t j=0; j < formulae.size() ;++j){
		Eval ev;// numbers calculator
		results[j] = static_cast<float>(
		ev.eval(      const_cast<char*>(
		        formulae[j].c_str())).real());
	}
	return results; };
}
auto abs_deltaphi(const float Zphi,const float Wphi)
	{return std::abs(delta_phi(Zphi-Wphi));}

auto jet_deltaphi(const floats& phis){
	if(debug>0) std::cout<<"jet deltaphi"<<std::endl;
	floats deltaphis;// half of anti-symmetric matrix has n(n-1)/2 elements
	deltaphis.reserve((phis.size()*(phis.size()-1))/2);// reserving size
	// The following two for loops stack correctly     // yet leave
	for(size_t   i=0; i < phis.size()-1 ;++i)	         // empty.
	for(size_t j=i+1; j < phis.size()   ;++j)
		deltaphis.push_back(abs_deltaphi(phis[i],phis[j]));
	return deltaphis;
}
auto find_lead_mask(const ints& mask,const floats& vals){
	if(debug>0) std::cout<<"lead mask"<<std::endl;
	if(!all_equal(mask.size(),vals.size())) throw std::logic_error(
		"Collections must be the same size in lead_mask");
	if(mask.empty()) throw std::logic_error(
		"Collections must not be empty in lead_mask");
	const auto      masked_vals = mask * vals;
	ints  lead_mask(masked_vals.size(),0);// vector of zeroes
	const auto max_idx = static_cast<size_t>(std::distance(
		masked_vals.cbegin(),
		max_element( masked_vals.cbegin(),
		             masked_vals.  cend())));// Leading bjet == highest pt.
	lead_mask[max_idx] = 1;
	return lead_mask;
}
auto find_z_pair(const floats& pts,
                 const floats& etas,
                 const floats& phis,
                 const floats& ms,
                 const   ints& tight_jets,
                 const   ints& lead_bjet){
	if(debug>0) std::cout<<"find z pair"<<std::endl;
	// This function finds the pair nearest to z mass
	double z_reco_mass = std::numeric_limits<double>::infinity();
	size_t jet_index_1 = std::numeric_limits<size_t>::max();
	size_t jet_index_2 = std::numeric_limits<size_t>::max();
	const size_t njets = pts.size();
	if(!all_equal(pts.size(),etas.size(),phis.size(),ms.size()))
		throw std::logic_error(
		"Collections must be the same size in Z-pair");
	if(pts.size()==0)	throw std::logic_error(
		"Collections must not be empty in Z-pair");
	ints z_pair(njets, 0);// vector of zeroes
	// The next two for loops stack correctly with the if
	for(size_t   i=0; i < njets-1 ;++i)// only try to z pair if
	for(size_t j=i+1; j < njets   ;++j)// both are tight jets and
	if(tight_jets[i] != 0 && tight_jets[j] != 0// NEITHER is a 
	&&  lead_bjet[i] == 0 &&  lead_bjet[j] == 0){// lead bjet
		TLorentzVector jet1,jet2;
		jet1.SetPtEtaPhiM(pts[i],etas[i],phis[i],ms[i]);
		jet2.SetPtEtaPhiM(pts[j],etas[j],phis[j],ms[j]);
		if (const double reco_mass = (jet1+jet2).M();
		std::abs(Z_MASS- reco_mass) < std::abs(Z_MASS-z_reco_mass)){
		   z_reco_mass = reco_mass;// found nearer pair to z mass
		   jet_index_1 = i;
		   jet_index_2 = j;
		}
	}
	z_pair[jet_index_1] = 1;
	z_pair[jet_index_2] = 1;
	if(debug>1) std::cout<<"z pair"<<z_pair<<std::endl;
	return z_pair;
}
auto TLVpairAdd(const floats& pt__pair,// Create TLorentzV from 2 jets 4-mom
                const floats& eta_pair,
                const floats& phi_pair,
                const floats& mas_pair){
	if(debug>0) std::cout<<"TLVpairAdd"<<std::endl;
	if(pt__pair.size() != 2) throw std::logic_error(
		"Not pair of Z (TLVcreator)");
	TLorentzVector v,p;
	v.SetPtEtaPhiM(pt__pair[0],eta_pair[0],phi_pair[0],mas_pair[0]);
	p.SetPtEtaPhiM(pt__pair[1],eta_pair[1],phi_pair[1],mas_pair[1]);
	return v+p;
}
auto TLVex(   const PtEtaPhiM         what){
   return [=](const TLorentzVector& object){
		if(debug>0) std::cout<<"TLVex"<<std::endl;
		float result;
		switch(what){
			case  pt:{result = static_cast<float>(object.Pt ());break;}
			case eta:{result = static_cast<float>(object.Eta());break;}
			case phi:{result = static_cast<float>(object.Phi());break;}
			case   m:{result = static_cast<float>(object. M ());break;}
//			default :throw std::invalid_argument(
//				"TLorentzVector extraction not recognised");
		}
		return result;
	};
}
/*
[[gnu::const]] auto inv_mass(
	const floats& pts,const floats& etas,const floats& phis,const floats& ms){
	if(debug>0) std::cout<<"inv mass"<<std::endl;
	if(!all_equal(pts.size(),etas.size(),phis.size(),ms.size()))
		throw std::logic_error("Collections must be the same size in inv_mass");
	if(pts.size() != 2)
		throw std::logic_error("Not pair of Z (inv_mass)");
	TLorentzVector vec,p;
	for(size_t i=0; i < pts.size() ;++i){
		// TODO: add error handling, don't allow more than two jets
		p.SetPtEtaPhiM(pts[i],etas[i],phis[i],ms[i]);
		vec += p;
	}
	if(debug>0) std::cout<<"inv mass "<< static_cast<float>(vec.M()) <<std::endl;
	return static_cast<float>(vec.M());
}*/
/*
// NOTE: abs_deltaphi is defined above and used
auto zw_deltaphi(const float& Zphi,const float& Wphi){
	return static_cast<float>(std::abs(delta_phi(Zphi-Wphi)));}
auto zmet_deltaphi = zw_deltaphi;
*/
/*
auto deltaphi_cut(const float    x){
      return  [=](const floats& dps){
		return std::any_of(dps.cbegin(),
		                   dps.  cend(),
		                   [](float dp){return dp >= x;});
	};
}*/
/*
auto zw_deltaphi_cut(const floats& deltaphi){
	return any_of(deltaphi.cbegin(),
	              deltaphi.  cend(),
	              [](float delta){return delta >= DELTA_PHI_ZW;});
}*/
/*
auto zmet_deltaphi_cut(const floats& deltaPhi){
	return any_of(deltaPhi.cbegin(),
	              deltaPhi.  cend(),
	              [](float delta){return delta >= DELTA_PHI_ZMET;});
}*/
auto top_reconst(const floats& bjets_pt,
                 const floats& bjets_eta,
                 const floats& bjets_phi,
                 const floats& bjets_mass,
                 const float & wpair_pt,// TODO: rename
                 const float & wpair_eta,
                 const float & wpair_phi,
                 const float & wpair_mass){
	if(debug>0) std::cout<<"top recnst"<<std::endl;
	// This function finds the closest to top mass
	float      cur_mass = std::numeric_limits<float>::infinity();
	const size_t nbjets = bjets_pt.size();
	if(!all_equal(nbjets,bjets_eta.size(),bjets_phi.size(),bjets_mass.size()))
		throw std::logic_error(
		"Collections must be the same size (top_recnst)");
	if(nbjets == 0) throw std::logic_error(
		"Collections must not be empty in  (top_recnst)");
	TLorentzVector BJets,RecoW,reco_top,temp;
		RecoW.SetPtEtaPhiM(wpair_pt   ,wpair_eta   ,wpair_phi   ,wpair_mass);
	// The following if for stacks correctly
	if(std::abs(RecoW.M() - W_MASS) < W_MASS_CUT)
	for(size_t i=0; i < nbjets ;++i){
		BJets.SetPtEtaPhiM(bjets_pt[i],bjets_eta[i],bjets_phi[i],bjets_mass[i]);
		temp = RecoW + BJets;
		if(float reco_mass = static_cast<float>(temp.M());
		    std::abs(TOP_MASS-reco_mass) < std::abs(TOP_MASS-cur_mass)){
			 cur_mass = reco_mass;// found closer to top mass
			reco_top  = temp;
		}
	}
	return reco_top;
}
auto BTaggedEffGiver(TH2D* &ratio){
	return [=](const floats& pts,const floats& etas){
	if(debug>0) std::cout<<"bt eff giver"<<std::endl;
	if(!all_equal(pts.size(),etas.size())) throw std::logic_error(
		"Collections must be the same size (EffGiver)");
	if(pts.empty()) throw std::logic_error(
		"Collections must not be empty in  (EffGiver)");
	floats BTaggedEff; BTaggedEff.reserve(pts.size());
	std::cout<<"BtaggedEff size "<<BTaggedEff.size()<<std::endl;
	for(size_t   i=0; i < pts.size() ;++i){
		int  PtBin = ratio->GetXaxis()->FindBin( pts[i]);
		std::cout<< "Pt Bin "<<PtBin<<std::endl;
		int EtaBin = ratio->GetYaxis()->FindBin(etas[i]);
		std::cout<< "Eta Bin "<<EtaBin<<std::endl;
		float  eff = static_cast<float>(ratio->GetBinContent(PtBin,EtaBin));
		std::cout<<"eff "<<eff<<std::endl;
		if(FP_NORMAL == std::fpclassify(eff))// if eff non-zero/inf/NaN
			BTaggedEff.push_back(eff);
		// above only pushed back nonzero nice eff
		// what do we do with eff==0? check with kathryn
	}
	return BTaggedEff;
};// did not indent the lambda
}
auto EffIsBTaggedProduct(const floats& EffIsBTagged){
	float      result  = 1.f;
	for(size_t i=0;  i < EffIsBTagged.size() ;++i)
	           result *= EffIsBTagged[i];
	return     result;
}
auto EffNoBTaggedProduct(const floats& EffNoBTagged){
	float  result  = 1.f;
	for(size_t i=0;  i < EffNoBTagged.size();++i)
	       result *= 1 - EffNoBTagged[i];
	return result;
}
auto Sfi_EffIsBTaggedProduct(const floats& EffIsBTagged,const floats& sfi){
	float  result = 1.f;
	size_t b = EffIsBTagged.size(), s = sfi.size();
	if(b!=s)std::cout<<"Sfi_EffIsBTaggedProduct got diff sizes"<<std::endl;
	size_t   size = b < s ? b : s;
	for(size_t i=0; i < size ;++i)
	       result    *= sfi[i] * EffIsBTagged[i];
	return result;
}
auto Sfj_EffNoBTaggedProduct(const floats& EffNoBTagged,const floats& sfj){
	float  result = 1.f;
	size_t b = EffNoBTagged.size(), s = sfj.size();
	if(b!=s)std::cout<<"Sfj_EffNoBTaggedProduct got diff sizes"<<std::endl;
	size_t   size = b < s ? b : s;
	for(size_t i=0; i < size ;++i)
	       result *=1 - EffNoBTagged[i] * sfj[i];
	return result;
}
	////////////// SCALE FACTORS /////////////
auto btag_weight(const float& p_data,const float& p_MC){
	float  weight = p_data / p_MC;
	return std::isinf(weight) || std::isnan(weight) ? 0.f : weight;
}
	// Normalization * btag weights
auto sf(const  dataSource ds){
	return [=](const float& b){
		float result;
		switch(ds){
			case tzq:{result =    TZQ_W;break;}
			case  ww:{result = WWLNQQ_W;break;}
			case  wz:{result = WZLNQQ_W;break;}
			case  zz:{result = ZZLLQQ_W;break;}
			case ttz:{result =  TTZQQ_W;break;}
			case met:// fall through to cms
			case cms:{result = 1.f;break;}// ignore btag wt
//			default :throw std::invalid_argument(
//				"Unimplemented ds (infile)");
		}
		return result * b;
	};
}
	// Event Weight, incl. btag & Normalization
auto rep_const(const float& sf,const floats& iRVec){
	// this function just repeats sf, for the size of iRVec
	floats weight(iRVec.size(),sf);
	return weight;
}
}// namespace
void calchisto(const channel ch,const dataSource ds){
	// Open data files even if unused
	// then programmatically choose which one to read from
	// No penalty for opening and leaving unused
	// Can even open multiple times at once in parallel
	
	// Open MC data source EVEN IF UNUSED
	std::string temp_header="/data/disk0/nanoAOD_2017/",
	temp_opener,temp_footer="/*.root";/**/
	switch(ds){// tzq and exptData use disk3!
	case tzq:{temp_opener="/data/disk3/nanoAOD_2017/tZqlvqq/*.root";break;}/**/
	case  ww:{temp_opener=temp_header+  "WWToLNuQQ"    +temp_footer;break;}
	case  wz:{temp_opener=temp_header+  "WZTo1L1Nu2Q"  +temp_footer;break;}
	case  zz:{temp_opener=temp_header+  "ZZTo2L2Q"     +temp_footer;break;}
	case ttz:{temp_opener=temp_header+ "ttZToQQ"       +temp_footer;break;}
	case met:{temp_opener=temp_header+ "ttZToQQ"       +temp_footer;break;}
	case cms:{temp_opener=temp_header+ "ttZToQQ"       +temp_footer;break;}
//	default :throw std::invalid_argument("Unimplemented ds (rdfopen)");
	}// CMS and MET MUST do some OPENABLE file ; reject later
	ROOT::RDataFrame mcdf{"Events",temp_opener};// Monte Carlo
	
	// Open chains of exptData EVEN IF UNUSED
	TChain elnuCMS("Events");
	TChain munuCMS("Events");
	temp_footer = "/*.root" ;/* just to be sure */
	temp_header =
		"/data/disk3/nanoAOD_2017/SingleElectron_NanoAOD25Oct2019_Run";
	for(std::string c:{"B","C","D","E","F"}){// guaranteed sequential
		temp_opener=temp_header+ c +temp_footer;
		elnuCMS.Add(temp_opener.c_str());
	}
	temp_header="/data/disk3/nanoAOD_2017/SingleMuon_NanoAOD25Oct2019_Run";
	for(std::string c:{"B","C","D","E","F"}){// guaranteed sequential
		temp_opener=temp_header+ c +temp_footer;
		munuCMS.Add(temp_opener.c_str());
	}
	ROOT::RDataFrame  metdf{"Events" ,// TODO: channel unified?
		"/data/disk0/nanoAOD_2017/MET*/*.root"};/**/
	ROOT::RDataFrame  elnudf(elnuCMS);
	ROOT::RDataFrame  munudf(munuCMS);
	ROOT::RDataFrame *pointerMagicRDF;
	// Need pointer magic to automatically get correct data
	switch(ds){
		case tzq:
		case  ww:// fall through!
		case  wz:
		case  zz:
		case ttz:{pointerMagicRDF =  &mcdf;break;}
		case met:{pointerMagicRDF = &metdf;break;}
		case cms:{switch(ch){
		           case elnu:{pointerMagicRDF = &elnudf;break;}
		           case munu:{pointerMagicRDF = &munudf;break;}
//		           default  :throw std::invalid_argument(
//			"Unimplemented ch (rdf set)");
			}break;}
//		default :throw std::invalid_argument(
//			"Unimplemented ds (rdf set)");
	}// using pointer magic in a few lines below
	switch(ch){// TODO: Email Ivan , how to submit jobs.
		case elnu:{temp_header = "Electron_";break;}
		case munu:{temp_header =     "Muon_";break;}
//		default  :throw std::invalid_argument(
//			"Unimplemented ch (init)");
	}
	ROOT::RDataFrame df = *pointerMagicRDF;// Finally!
	// make test runs faster by restriction. Real run should not
	auto dfr = df.Range(50);
	auto w_selection = dfr// remove one letter to do all
	.Filter(met_pt_cut(ch),{"MET_pt"},"MET Pt cut")
	.Define("loose_leps",lep_sel(ch),
	       {temp_header+"isPFcand",
	        temp_header+"pt" ,
	        temp_header+"eta",
	        "Electron_cutBased",
	        "Muon_tightId",
	        "Muon_pfRelIso04_all"})
	.Filter(lep_tight_cut(ch),{"loose_leps",
	        "Electron_cutBased",// left with 1 tight lepton, no loose
	        "Muon_pfRelIso04_all"},"lepton cut")
	.Define(   "lep__pt",temp_header+  "pt[loose_leps][0]")
	.Define(   "lep_eta",temp_header+ "eta[loose_leps][0]")
	.Define(   "lep_phi",temp_header+ "phi[loose_leps][0]")
	.Define(   "lep_mas",temp_header+"mass[loose_leps][0]")
	. Alias("tw_lep__pt" ,"lep__pt")
	. Alias("tw_lep_eta" ,"lep_eta")
	. Alias("tw_lep_phi" ,"lep_phi")
	.Define("tw_lep_mas",transverse_w_mass,
	       {"tw_lep__pt",
	        "tw_lep_phi","MET_pt","MET_phi"})
	.Define(   "lep_nu_invmass",lep_nu_invmass,
	       {   "lep__pt",
	           "lep_eta",
	           "lep_phi",
	           "lep_mas",
	       "CaloMET_pt" ,
	       "CaloMET_phi"})
//	       "CaloMET_sumEt"})// TODO: add this back
//	.Filter(w_mass_cut, {"w_el_mass"}, "W mass cut")
	;
	
	// There is a Histogram1D done on w_selection
	
	auto jet_selection
	   = w_selection
	.Define("jet_lep_min_dR" , jet_lep_min_deltaR,
	       {"Jet_eta","Jet_phi","lep_eta","lep_phi"})
	.Define("tight_jets"     , tight_jet_id   ,
	       {"jet_lep_min_dR" ,  "Jet_pt","Jet_eta","Jet_jetId"})
	.Filter(jetCutter(JETS_MIN,JETS_MAX),{"tight_jets"},"Jet cut")
	.Define("tight_jets__pt" ,  "Jet_pt[tight_jets]")
	.Define("tight_jets_eta" , "Jet_eta[tight_jets]")
	.Define("tight_jets_phi" , "Jet_phi[tight_jets]")
	.Define("tight_jets_mas" ,"Jet_mass[tight_jets]")
	;
	// JEC == tight_jets inc. Jet Energy Correction
	// good_jets (defined above) are tight jets which have generated level info
	// by neglecting the ones which exclude generated level info,
	// we make sure the jec is computing correctly
	// & jets w/o JEC will be excluded too.
	auto jec // DONE: CMS and MET no GenJet
	   = jet_selection
	.Define("good_jets__pt" ,jets_gen_select,{"GenJet_pt"  ,"tight_jets__pt"})
	.Define("good_jets_eta" ,jets_gen_select,{"GenJet_eta" ,"tight_jets_eta"})
	.Define("good_jets_phi" ,jets_gen_select,{"GenJet_phi" ,"tight_jets_phi"})
	.Define("good_jets_mas" ,jets_gen_select,{"GenJet_mass","tight_jets_mas"})
	.Define("pt_resol"     , jet_smear_pt_resol,
	       {"good_jets__pt",
	        "good_jets_eta","fixedGridRhoFastjetAll"})
	.Define("Sjer",      jet_smear_Sjer  , {"good_jets_eta"})
	.Define("cjer",    delta_R_jet_smear , {"good_jets__pt","GenJet_pt",
	        "pt_resol","Sjer","jet_lep_min_dR"})
	.Define("fin_jets__pt" , "good_jets__pt * cjer")// these are JEC
	.Define("fin_jets_eta" , "good_jets_eta * cjer")// Monte Carlo
	.Define("fin_jets_phi" , "good_jets_phi * cjer")// needs JEC
	.Define("fin_jets_mas" , "good_jets_mas * cjer")// fin = final
	;
	auto jecs_bjets
	   = jec
	.Define("is_bjets"         ,is_bjet_id   ,{"fin_jets_eta","Jet_btagCSVV2"})
	.Filter(jetCutter(BJETS_MIN,BJETS_MAX)   ,{"is_bjets"},"b jet cut")
	.Define("no_bjets"         ,no_bjet_id   ,{"fin_jets_eta"})
	.Define("is_btag_numer"    ,is_bjet_numer,{"Jet_partonFlavour","is_bjets"})
	.Define("no_btag_numer"    ,no_bjet_numer,{"Jet_partonFlavour","is_bjets"})
	.Define("is_btag_denom"    ,is_bjet_denom,{"Jet_partonFlavour","no_bjets"})
	.Define("no_btag_denom"    ,no_bjet_denom,{"Jet_partonFlavour","no_bjets"})
	.Define("is_btag_numer__pt","fin_jets__pt[is_btag_numer]")
	.Define("no_btag_numer__pt","fin_jets__pt[no_btag_numer]")
	.Define("is_btag_denom__pt","fin_jets__pt[is_btag_denom]")
	.Define("no_btag_denom__pt","fin_jets__pt[no_btag_denom]")
	.Define("is_btag_numer_eta","fin_jets_eta[is_btag_numer]")
	.Define("no_btag_numer_eta","fin_jets_eta[no_btag_numer]")
	.Define("is_btag_denom_eta","fin_jets_eta[is_btag_denom]")
	.Define("no_btag_denom_eta","fin_jets_eta[no_btag_denom]")
	.Define("sfi",btagCSVv2( true),// checks btag
	       { "Jet_btagCSVV2","fin_jets__pt","fin_jets_eta"})
	.Define("sfj",btagCSVv2(false),// ignore btag
	       { "Jet_btagCSVV2","fin_jets__pt","fin_jets_eta"})
	;
	auto expt_bjets
	   = jet_selection
	. Alias("fin_jets__pt" , "tight_jets__pt")
	. Alias("fin_jets_eta" , "tight_jets_eta")
	. Alias("fin_jets_phi" , "tight_jets_phi")// TODO: ALWAYS check copy of
	. Alias("fin_jets_mas" , "tight_jets_mas")// next two lines from above
	.Define("is_bjets"         ,is_bjet_id   ,{"fin_jets_eta","Jet_btagCSVV2"})
	.Filter(jetCutter(BJETS_MIN,BJETS_MAX)   ,{"is_bjets"},"b jet cut")
	;
	decltype(expt_bjets) *bjets_correctly;
	switch(ds){
		case tzq:
		case  ww:// fall through!
		case  wz:
		case  zz:
		case ttz:{bjets_correctly = &jecs_bjets;break;}
		case met:
		case cms:{bjets_correctly = &expt_bjets;break;}
//		default :throw std::invalid_argument(
//			"Unimplemented ds (bjets)");
	}
	auto z___reco
	   = (*bjets_correctly)
	.Define("fin_jets_deltaphi",jet_deltaphi,{"fin_jets_phi"})
	.Define("lead_bjet"     ,find_lead_mask,{"is_bjets","fin_jets__pt"})
	.Define(     "bjet__pt" ,"fin_jets__pt[lead_bjet]")// Leading bjet
	.Define(     "bjet_eta" ,"fin_jets_eta[lead_bjet]")// 4-momentum
	.Define(     "bjet_phi" ,"fin_jets_phi[lead_bjet]")// used for top
	.Define(     "bjet_mas" ,"fin_jets_mas[lead_bjet]")// reconstruction
	.Define( "z_reco_jets"  , find_z_pair  ,
	       {"fin_jets__pt"  ,
	        "fin_jets_eta"  ,
	        "fin_jets_phi"  ,//easier to push back
	        "fin_jets_mas"  , "tight_jets" ,"lead_bjet" })
	.Define(  "z_pair__pt"  , "fin_jets__pt[z_reco_jets]")
	.Define(  "z_pair_eta"  , "fin_jets_eta[z_reco_jets]")
	.Define(  "z_pair_phi"  , "fin_jets_phi[z_reco_jets]")
	.Define(  "z_pair_mas"  , "fin_jets_mas[z_reco_jets]")
	.Define(  "z_TLV"	      , TLVpairAdd,
	       {  "z_pair__pt"  ,
	          "z_pair_eta"  ,
	          "z_pair_phi"  ,
	          "z_pair_mas" })// TODO: why should we unpack? below
	.Define(  "z_mas"       , TLVex( m ),{"z_TLV"})
	.Filter(   z_mass_cut   , { "z_mas"},"z mass cut")
	.Define(  "z__pt"       , TLVex(pt ),{"z_TLV"})
	.Define(  "z_eta"       , TLVex(eta),{"z_TLV"})
	.Define(  "z_phi"       , TLVex(phi),{"z_TLV"})
	.Define(  "z_lep_min_dR", jet_lep_min_deltaR,// TODO: Check input?
	       {  "z_pair_eta"  ,
	          "z_pair_phi"  ,
	             "lep_eta"  ,
	             "lep_phi" })// TODO: wrong input below
//	.Filter(      deltaR_z_l,  {  "z_lep_min_dR"},"delta R ZL")
	.Define(  "zw_deltaphi" , abs_deltaphi,{"z_phi","tw_lep_phi"})
//	.Filter(      deltaphi_cut(DELTA_PHI_ZW),
//	       {  "zw_deltaphi"},"delta phi ZW cut")
	.Define("zmet_deltaphi" , abs_deltaphi,{"z_phi","MET_phi"})
//	.Filter(      deltaphi_cut(DELTA_PHI_ZMET),
//	       {"zmet_deltaphi"}, "Z met cut ");
	;
	auto top_reco
	   = z___reco
	.Define("recoTtop",top_reconst,
	       {"bjet__pt",
	        "bjet_eta",
	        "bjet_phi",
	        "bjet_mas",
	      "tw_lep__pt",
	      "tw_lep_eta",
	      "tw_lep_phi",
	      "tw_lep_mas"})
	.Define("ttop__pt",TLVex(pt ),{"recoTtop"})
	.Define("ttop_eta",TLVex(eta),{"recoTtop"})
	.Define("ttop_phi",TLVex(phi),{"recoTtop"})
	.Define("ttop_mas",TLVex( m ),{"recoTtop"})
	;// TODO: CMS and MET stop here
	// now we make the histogram names and titles
	switch(ch){// laugh at muon-neutrino below
		case elnu:{temp_header = "elnu_";
		           temp_footer = "electron-neutrino";break;}
		case munu:{temp_header = "munu_";
		           temp_footer = "muon"  "-neutrino";break;}
//		default  :throw std::invalid_argument(
//			"Unimplemented ch (hist titles)");
	}
	temp_footer = "pt vs eta in" + temp_footer + " channel for ";
	switch(ds){
		case tzq:{temp_header+="tzq";temp_footer+="tZq";break;}
		case  ww:{temp_header+="_ww";temp_footer+=" WW";break;}
		case  wz:{temp_header+="_wz";temp_footer+=" WZ";break;}
		case  zz:{temp_header+="_zz";temp_footer+=" ZZ";break;}
		case ttz:{temp_header+="ttz";temp_footer+="ttZ";break;}
		case met:{temp_header+="met";temp_footer+="MET";break;}
		case cms:{temp_header+="cms";temp_footer+="CMS";break;}
//		default :throw std::invalid_argument(
//			"Unimplemented ds (hist titles)");
	}
	
	auto h_is_btag_numer_PtVsEta
	   = top_reco
	.Histo2D({static_cast<const char*>(
	          (        "is_numer_" + temp_header).c_str()),
	          static_cast<const char*>(
	          ("MC is btag numer " + temp_footer).c_str()),
	          50,0,400,50,-3,3},
	              "is_btag_numer__pt",
	              "is_btag_numer_eta");
	
	auto h_no_btag_numer_PtVsEta
	   = top_reco
	.Histo2D({static_cast<const char*>(
	          (        "no_numer_" + temp_header).c_str()),
	          static_cast<const char*>(
	          ("MC no btag numer " + temp_footer).c_str()),
	          50,0,400,50,-3,3},
	              "no_btag_numer__pt",
	              "no_btag_numer_eta");
        
	auto h_is_btag_denom_PtVsEta
	   = top_reco
	.Histo2D({static_cast<const char*>(
	          (        "is_denom_" + temp_header).c_str()),
	          static_cast<const char*>(
	          ("MC is btag denom " + temp_footer).c_str()),
	          50,0,400,50,-3,3},
	              "is_btag_denom__pt",
	              "is_btag_denom_eta");
        
	auto h_no_btag_denom_PtVsEta
	   = top_reco
	.Histo2D({static_cast<const char*>(
	          (        "no_denom_" + temp_header).c_str()),
	          static_cast<const char*>(
	          ("MC no btag denom " + temp_footer).c_str()),
	          50,0,400,50,-3,3},
	              "no_btag_denom__pt",
	              "no_btag_denom_eta");
	
	TH2D *
	is_btag_ratio = new TH2D("ei", "is b tag ei",50,0,400,50,-3,3);
	is_btag_ratio = static_cast<TH2D*>(h_is_btag_numer_PtVsEta->Clone());
	// This is a test before the divide
	std::cout<< "h_btag_numer before divide "<< h_is_btag_numer_PtVsEta.GetPtr()<<std::endl;
	is_btag_ratio->Divide(             h_is_btag_denom_PtVsEta.GetPtr());
	std::cout<<"this is the btag numer after divide "<< h_is_btag_numer_PtVsEta.GetPtr()<<std::endl;
	TH2D *
	no_btag_ratio = new TH2D("ej", "no b tag ei",50,0,400,50,-3,3);
	no_btag_ratio = static_cast<TH2D*>(h_no_btag_numer_PtVsEta->Clone());
        std::cout<<"h_no_btag_numer before divide"<<h_no_btag_numer_PtVsEta.GetPtr()<<std::endl;
	no_btag_ratio->Divide(             h_no_btag_denom_PtVsEta.GetPtr());
        std::cout<<"h_no_btag_numer after divide"<<h_no_btag_numer_PtVsEta.GetPtr()<<std::endl;

	
	auto btag_eff
	   = top_reco
	.Define("IsEffBTagged",BTaggedEffGiver(is_btag_ratio),
	       {"fin_jets__pt","fin_jets_eta"})
	.Define("NoEffBTagged",BTaggedEffGiver(no_btag_ratio),
	       {"fin_jets__pt","fin_jets_eta"})
	;
	auto P_btag
	   = btag_eff
	.Define("Pi___ei",     EffIsBTaggedProduct,{"IsEffBTagged"})
	.Define("Pi___ej",     EffNoBTaggedProduct,{"NoEffBTagged"})
	.Define("Pi_sfei", Sfi_EffIsBTaggedProduct,{"IsEffBTagged","sfi"})
	.Define("Pi_sfej", Sfj_EffNoBTaggedProduct,{"NoEffBTagged","sfj"})
	.Define("P_MC"   , "Pi___ei * Pi___ej")
	.Define("P_Data" , "Pi_sfei * Pi_sfej")
	.Define("btag_w" , btag_weight,{"P_Data","P_MC"})
	.Define("sf"     , sf(ds)     ,{"btag_w"})
	. Alias("nw_lep__pt"          ,"sf")// is just one value, == sf
	. Alias("nw_lep_eta"          ,"sf")// LOL WHY SO DUMB, weigh the
	. Alias("nw_lep_phi"          ,"sf")// hist BY "sf" then!
	. Alias("nw_lep_mas"          ,"sf")
	.Define("nw_fin_jets__pt"     ,rep_const,{"sf","fin_jets__pt"  })
	.Define("nw_fin_jets_eta"     ,rep_const,{"sf","fin_jets_eta"  })
	.Define("nw_fin_jets_phi"     ,rep_const,{"sf","fin_jets_phi"  })
	.Define("nw_fin_jets_mas"     ,rep_const,{"sf","fin_jets_mas"  })
	.Define("nw_jet_lep_min_dR"   ,rep_const,{"sf","jet_lep_min_dR"})
	.Define( "nw__z_lep_min_dR"   ,rep_const,{"sf",  "z_lep_min_dR"})
	. Alias( "nw_tw_lep_mas"      ,"sf")
	. Alias( "nw__z_mas"          ,"sf")
	.Define("nw_fin_jets_deltaphi",rep_const,{"sf","fin_jets_deltaphi"})
	. Alias(    "nw_zmet_deltaphi","sf")
	. Alias(      "nw_zw_deltaphi","sf")
//	. Alias("nw_ttop__pt","sf")
//	. Alias("nw_ttop_mas","sf")
	;
// BOSONs hists
	auto
	h_trans_w = P_btag.Histo1D({
	static_cast<const char*>((          "tWm_"     + temp_header).c_str()),
	static_cast<const char*>(("Transverse W mass " + temp_header).c_str()),
	50,0,180},
	"tw_lep_mas","sf");
	h_trans_w->GetXaxis()->SetTitle("mass GeV/C^2");
	h_trans_w->GetYaxis()->SetTitle("Event");
	
	auto h_invmass// lepton-neutrino invariant mass histogram
	   = w_selection
	.Histo1D({
	static_cast<const char*>((         "invmass_" + temp_header).c_str()),
	static_cast<const char*>((         "invmass_" + temp_header).c_str()),
	50,0,200},
	"lep_nu_invmass");// TODO: These two histograms??
	auto
	h_Winvmass = P_btag.Histo1D({
	static_cast<const char*>(("W_invariant_mass_" + temp_header).c_str()),
	static_cast<const char*>(("W invariant mass " + temp_header).c_str()),
	50,0,180},
	"lep_nu_invmass","sf");
	h_Winvmass->GetXaxis()->SetTitle("mass GeV/C^2");
	h_Winvmass->GetYaxis()->SetTitle("Event");
	
	
	
	
	auto
	h_tWmVsZmass_calc = P_btag.Histo2D({
		// TODO: DECLARE ALL HISTO WITH THIS TECHNIQUE
		static_cast<const char*>(("tWmVsZmass" + temp_header).c_str()),
		static_cast<const char*>(("tWmVsZmass" + temp_header).c_str()),
		50,0,200,50,0,200},
		"tw_lep_mas","z_mas");
	h_tWmVsZmass_calc->GetXaxis()->SetTitle("tWm   GeV/C^2");
	h_tWmVsZmass_calc->GetYaxis()->SetTitle("Zmass GeV/C^2");
	
	// write histograms to a root file
	// ASSUMES temp_header is correct!
	TFile hf(
	 static_cast<const char*>((temp_header+".histo").c_str())
	,"RECREATE");
	
	h_invmass->Write();
	h_is_btag_numer_PtVsEta->Write();
	h_no_btag_numer_PtVsEta->Write();
	h_is_btag_denom_PtVsEta->Write();
	h_no_btag_denom_PtVsEta->Write();
//	h_transTopmass->Write();
	h_tWmVsZmass_calc->Write();
	// the following two for loops stack correctly
	for(std::string particle:{"fin_jets","lep"})
	for(PtEtaPhiM k:PtEtaPhiMall){
		std::string kstring  = "_";
		double xmin,xmax;
		switch(k){
			case pt :{kstring+= "_pt";xmin =  0 ;xmax = 200;break;}
			case eta:{kstring+= "eta";xmin = -3 ;xmax =  3 ;break;}
			case phi:{kstring+= "phi";xmin = -7 ;xmax =  7 ;break;}
			case  m :{kstring+= "mas";xmin =  0 ;xmax =  30;break;}
//			default :throw std::invalid_argument(
//				"Unimplemented component (histo)");
		}
		auto
		h = P_btag.Histo1D({
		 static_cast<const char*>(   (temp_header+kstring).c_str())
		,static_cast<const char*>(   (temp_header+kstring).c_str())
		,50,xmin,xmax}
		,static_cast<const char*>(      (particle+kstring).c_str())
		,static_cast<const char*>(("nw_"+particle+kstring).c_str())
		);
		/* 
		 * TODO: These are too long for above switch case
		 * And they should be in plotstacks anyway
		h->GetXaxis()->SetTitle("pT/GeV");
		h->GetXaxis()->SetTitle("PseudoRapidity eta");
		h->GetXaxis()->SetTitle("Azimuthal angle, phi/rad");
		h->GetXaxis()->SetTitle("mass GeV/C^2");
		h->GetYaxis()->SetTitle("Event");
		 */
		h->Write();
	}
	
	hf.Close();
}
