// system includes
#include <iostream>
#include <cmath>
#include <algorithm>
#include <unordered_map>

// ROOT includes
#include "TH1D.h"
#include "TH2F.h"
#include "TH1F.h"
#include "TTree.h"
#include "TFile.h"
#include "TGraphAsymmErrors.h"
#include "RooWorkspace.h"
#include "RooRealVar.h"
#include "RooFunctor.h"
#include "RooMsgService.h"

// user includes
#include "include/util.h"
#include "include/event_info.h"
#include "include/tau_factory.h"
#include "include/muon_factory.h"
#include "include/jet_factory.h"
#include "include/met_factory.h"
#include "include/SF_factory.h"
// #include "include/util_mt.h"
#include "include/btagSF.h"
#include "include/LumiReweightingStandAlone.h"
#include "include/CLParser.h"

int main(int argc, char* argv[]) {

  ////////////////////////////////////////////////
  // Initial setup:                             //
  // Get file names, normalization, paths, etc. //
  ////////////////////////////////////////////////

  CLParser parser(argc, argv);
  bool local = parser.Flag("-l");
  std::string sample = parser.Option("-s");
  std::string name = parser.Option("-n");
  std::string path = parser.Option("-p");
  std::string syst = parser.Option("-u");
  std::string postfix = parser.Option("-P");
  std::string fname = path + sample + postfix;
  bool isData = sample.find("Data") != std::string::npos;
  // bool isData = parser.Flag("-d");
  std::string systname = "";
  if (!syst.empty()) {
    systname = "_" + syst;
  }

  // open input file
  std::cout << "Opening file... " << sample << std::endl;
  auto fin = TFile::Open((fname+".root").c_str());
  std::cout << "Loading Ntuple..." << std::endl;
  auto ntuple = (TTree*)fin->Get("mutau_tree");

  // get number of generated events
  auto counts = (TH1D*)fin->Get("nevents");
  auto gen_number = counts->GetBinContent(2);

  // create output file
  auto suffix = "_output.root";
  auto prefix = "output/";
  std::string filename;
  if (name == sample) {
    filename = prefix + name + systname + suffix;
  } else {
    filename = prefix + sample + std::string("_") + name + systname + suffix;
  }
  auto fout = new TFile(filename.c_str(), "RECREATE");
  fout->mkdir("grabbag");
  fout->cd("grabbag");

  // initialize Helper class
  Helper helper(fout, name, syst);

  // get normalization (lumi & xs are in util.h)
  double norm;
  if (isData)
    norm = 1.0;
  else
    norm = helper.getLuminosity() * helper.getCrossSection(sample) / gen_number;

  ///////////////////////////////////////////////
  // Scale Factors:                            //
  // Read weights, hists, graphs, etc. for SFs //
  ///////////////////////////////////////////////

  // read inputs for lumi reweighting
  auto lumi_weights = new reweight::LumiReWeighting("inputs/MC_Moriond17_PU25ns_V1.root", "inputs/Data_Pileup_2016_271036-284044_80bins.root", "pileup", "pileup");

  // tracking corrections
  TFile *f_Trk = new TFile("inputs/Tracking_EfficienciesAndSF_BCDEFGH.root");
  TH2F *h_Trk = (TH2F*)f_Trk->Get("ratio_eff_eta3_dr030e030_corr");

  // Z-pT reweighting
  TFile *zpt_file = new TFile("inputs/zpt_weights_2016_BtoH.root");
  auto zpt_hist = (TH2F*)zpt_file->Get("zptmass_histo");

  //H->tau tau scale factors
  TFile htt_sf_file("inputs/htt_scalefactors_sm_moriond_v1.root");
  RooWorkspace *htt_sf = (RooWorkspace*)htt_sf_file.Get("w");
  htt_sf_file.Close();


  // trigger and ID scale factors
  auto myScaleFactor_trgMuon24 = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_IsoMu24_OR_TkIsoMu24_2016BtoH_eff.root");
  auto myScaleFactor_trgMu19Leg = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_Mu19leg_2016BtoH_eff.root");
  auto myScaleFactor_trgMu22 = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_Mu22OR_eta2p1_eff.root");
  auto myScaleFactor_trgMu19LegAnti = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_Mu19leg_eta2p1_antiisolated_Iso0p15to0p3_eff_rb.root");
  auto myScaleFactor_trgMu22Anti = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_Mu22OR_eta2p1_antiisolated_Iso0p15to0p3_eff_rb.root");
  auto myScaleFactor_id = new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_IdIso_IsoLt0p15_2016BtoH_eff.root");
  auto myScaleFactor_idAnti= new SF_factory("LeptonEfficiencies/Muon/Run2016BtoH/Muon_IdIso_antiisolated_Iso0p15to0p3_eff_rb.root");

  TFile * f_NNLOPS = new TFile("inputs/NNLOPS_reweight.root");
  TGraph * g_NNLOPS_0jet = (TGraph*) f_NNLOPS-> Get("gr_NNLOPSratio_pt_powheg_0jet");
  TGraph * g_NNLOPS_1jet = (TGraph*) f_NNLOPS-> Get("gr_NNLOPSratio_pt_powheg_1jet");
  TGraph * g_NNLOPS_2jet = (TGraph*) f_NNLOPS-> Get("gr_NNLOPSratio_pt_powheg_2jet");
  TGraph * g_NNLOPS_3jet = (TGraph*) f_NNLOPS-> Get("gr_NNLOPSratio_pt_powheg_3jet");

  //////////////////////////////////////
  // Final setup:                     //
  // Declare histograms and factories //
  //////////////////////////////////////

  // declare histograms (histogram initializer functions in util.h)
  fout->cd("grabbag");
  auto histos = helper.getHistos1D();
  auto histos_2d = helper.getHistos2D();

  // construct factories
  event_info       event(ntuple, syst, "mt");
  muon_factory     muons(ntuple);
  tau_factory      taus(ntuple);
  jet_factory      jets(ntuple, syst);
  met_factory      met(ntuple, syst);
  double n70_count;

  // begin the event loop
  Int_t nevts = ntuple->GetEntries();
  for (Int_t i = 0; i < nevts; i++) {
    ntuple->GetEntry(i);
    if (i % 1000 == 0)
      std::cout << "Processing event: " << i << " out of " << nevts << std::endl;

    // find the event weight (not lumi*xs if looking at W or Drell-Yan)
    double evtwt(norm), corrections(1.), sf_trig(1.), sf_trig_anti(1.), sf_id(1.), sf_id_anti(1.);
    if (name == "W") {
      if (event.getNumGenJets() == 1)
        evtwt = 6.8176;
      else if (event.getNumGenJets() == 2)
        evtwt = 2.1038;
      else if (event.getNumGenJets() == 3)
	evtwt = 0.6889;
      else if (event.getNumGenJets() == 4)
        evtwt = 0.6900;
      else
        evtwt = 25.446;
    }

    if (name == "ZTT" || name == "ZLL" || name == "ZL" || name == "ZJ") {
      if (event.getNumGenJets() == 1)
        evtwt = 0.45729;
      else if (event.getNumGenJets() == 2)
        evtwt = 0.4668;
      else if (event.getNumGenJets() == 3)
        evtwt = 0.47995;
      else if (event.getNumGenJets() == 4)
        evtwt = 0.39349;
      else
        evtwt = 1.4184;
    }

    // fout->cd("grabbag");
    histos->at("cutflow")->Fill(0., 1.);

    /////////////////////////////////////////////////////////////////////
    // Event Selection:                                                //
    //   - Trigger:                                                    //
    //     * Cross ( muon pT <= 23 )                                   //
    //       IsoMu19Tau20                                              //
    //     * SingleLep ( muon pT > 23 )                                //
    //       IsoMu22 || IsoTkMu22 || IsoMu22eta2p1 || IsoTkMu22eta2p1  //
    //   - Muon: pT > 20, |eta| < 2.1                                  //
    //   - Tau: pT > 30, |eta| < 2.3                                   //
    /////////////////////////////////////////////////////////////////////

    // muon pT > 20 GeV
    auto muon = muons.run_factory();
    if (muon.getPt() > 20 && fabs(muon.getEta()) < 2.1)  histos->at("cutflow") -> Fill(1., 1);
    else continue;

    // low energy muon passes IsoMu19Tau20
    if (muon.getPt()<=23 && (event.getPassCrossTrigger()))
	histos->at("cutflow") -> Fill(2., 1);
    // high energy muon passes IsoMu22 || IsoTkMu22 || IsoMu22eta2p1 || IsoTkMu22eta2p1
    else if(muon.getPt()>23 && event.getPassIsoMu22() && event.getPassIsoTkMu22() && event.getPassIsoMu22eta2p1() && event.getPassIsoTkMu22eta2p1())
    	histos->at("cutflow") -> Fill(2., 1);
    else continue;

    // tau pT > 30 and |eta| < 2.3
    auto tau = taus.run_factory();
    if (tau.getPt() > 30 && fabs(tau.getEta()) < 2.3) histos->at("cutflow") -> Fill(3., 1);
    else continue;

    // check against mu/el
    //if (tau.getAgainstVLooseElectron() && tau.getAgainstTightMuon()) histos->at("cutflow") -> Fill(4., 1);
    /*
    std::cout << "i : " << i << std::endl;
    std::cout << "tau.getAgainstVLooseElectron() : " << tau.getAgainstVLooseElectron() << std::endl;
    std::cout << "tau.getAgainstTightMuon() : " << tau.getAgainstTightMuon() << std::endl;
    if (tau.getAgainstVLooseElectron())     std::cout << "norm1 : " << norm << std::endl;    
    if (tau.getAgainstTightMuon())     std::cout << "norm2 : " << norm << std::endl;    
    else continue;
    */
    // end event selection
    std::cout << "Doyeong" << std::endl;
    // get jet data for the event
    jets.run_factory();

    // build Higgs
    TLorentzVector Higgs = muon.getP4() + tau.getP4() + met.getP4();

    // Separate Drell-Yan
    if (name == "ZL" && tau.getGenMatch() > 4)
      continue;
    else if ((name == "ZTT" || name == "TTT") && tau.getGenMatch() != 5)
      continue;
    else if ((name == "ZLL" || name == "TTJ") && tau.getGenMatch() == 5)
      continue;
    else if (name == "ZJ" && tau.getGenMatch() != 6)
      continue;

    histos->at("cutflow") -> Fill(6., 1.);

    // apply all scale factors/corrections/etc.
    if (!isData) {
      std::cout << "Doyeong" << std::endl;
      // apply trigger and id SF's
      sf_id        = myScaleFactor_id->getSF(muon.getPt(), muon.getEta());
      sf_id_anti   = myScaleFactor_idAnti->getSF(muon.getPt(), muon.getEta());
      
      // tau ID efficiency SF
      if (tau.getGenMatch() == 5)
        evtwt *= 0.95;
      float eff_tau = 1.0;
      float eff_tau_ratio = 1.0;
      if (muon.getPt()<23) {
	htt_sf->var("t_pt")->setVal(tau.getPt());
	htt_sf->var("t_eta")->setVal(tau.getEta());
	htt_sf->var("t_dm")->setVal(tau.getDecayModeFinding());
	//evtwt *= htt_sf->function("t_genuine_TightIso_mt_ratio")->getVal();
	eff_tau_ratio = htt_sf->function("t_genuine_TightIso_mt_ratio")->getVal();
	sf_trig       = myScaleFactor_trgMu19Leg->getSF(muon.getPt(),muon.getEta())*eff_tau_ratio;
	sf_trig_anti  = myScaleFactor_trgMu19LegAnti->getSF(muon.getPt(),muon.getEta())*eff_tau_ratio;
      }
      else{
	sf_trig       = myScaleFactor_trgMu22->getSF(muon.getPt(),muon.getEta());
	sf_trig_anti  = myScaleFactor_trgMu22Anti->getSF(muon.getPt(),muon.getEta());
      }
      evtwt *= (sf_trig * sf_id * lumi_weights->weight(event.getNPU()) * event.getGenWeight());  
      
      // // anti-lepton discriminator SFs
      if (tau.getGenMatch() == 2 or tau.getGenMatch() == 4){//Yiwen reminiaod
	if (fabs(tau.getEta())<0.4) evtwt *= 1.263;
	else if (fabs(tau.getEta())<0.8) evtwt *= 1.364;
	else if (fabs(tau.getEta())<1.2) evtwt *= 0.854;
	else if (fabs(tau.getEta())<1.7) evtwt *= 1.712;
	else if (fabs(tau.getEta())<2.3) evtwt *= 2.324;
	if (name == "ZL" && tau.getL2DecayMode() == 0) evtwt *= 0.74; //ZL corrections Laura
	else if (name == "ZL" && tau.getL2DecayMode() == 1) evtwt *= 1.0;
      }
      if (tau.getGenMatch() == 1 or tau.getGenMatch() == 3){//Yiwen
	if (fabs(tau.getEta())<1.460) evtwt *= 1.213;
	else if (fabs(tau.getEta())>1.558) evtwt *= 1.375;
      }

      // Z-pT and Zmm Reweighting
      if (name=="EWKZLL" || name=="EWKZNuNu" || name=="ZTT" || name=="ZLL" || name=="ZL" || name=="ZJ") {
        evtwt *= zpt_hist->GetBinContent(zpt_hist->GetXaxis()->FindBin(event.getGenM()),zpt_hist->GetYaxis()->FindBin(event.getGenPt()));
        evtwt *= GetZmmSF(jets.getNjets(), jets.getDijetMass(), Higgs.Pt(), tau.getPt(), 0);
      } 

      // // top-pT Reweighting (only for some systematic)
      // if (name == "TTT" || name == "TT" || name == "TTJ") {
      //   float pt_top1 = std::min(float(400.), jets.getTopPt1());
      //   float pt_top2 = std::min(float(400.), jets.getTopPt2());
      //   evtwt *= sqrt(exp(0.0615-0.0005*pt_top1)*exp(0.0615-0.0005*pt_top2));
      // }
      // b-tagging SF (only used in scaling W, I believe)
      int nbtagged = std::min(static_cast<Float_t>(2), jets.getNbtag());
      auto bjets = jets.getBtagJets();
      float weight_btag( bTagEventWeight(nbtagged, bjets.at(0).getPt() ,bjets.at(0).getFlavor(), bjets.at(1).getPt(), bjets.at(1).getFlavor() ,1,0,0) );
      if (nbtagged>2) weight_btag=0;
    }
    fout->cd();

    histos->at("cutflow") -> Fill(11, 1.);

    // calculate mt
    double met_x = met.getMet() * cos(met.getMetPhi());
    double met_y = met.getMet() * sin(met.getMetPhi());
    double met_pt = sqrt(pow(met_x, 2) + pow(met_y, 2));
    double mt = sqrt(pow(muon.getPt() + met_pt, 2) - pow(muon.getPx() + met_x, 2) - pow(muon.getPy() + met_y, 2));
    int evt_charge = tau.getCharge() + muon.getCharge();

    // DK
    if (mt > 80 && mt < 200 && evt_charge == 0 && tau.getTightIsoMVA() && muon.getIso() < 0.10) {
      histos->at("n70") -> Fill(0.1, evtwt);
      if (jets.getNjets() == 0 && event.getMSV() < 400)
        histos->at("n70") -> Fill(1.1, evtwt);
      else if (jets.getNjets() == 1 || (jets.getNjets() > 1 && jets.getDijetMass() > 300 && Higgs.Pt() < 100))
        histos->at("n70") -> Fill(2.1, evtwt);
      else if (jets.getNjets() > 1 && jets.getDijetMass() > 300 && Higgs.Pt() > 100)
        histos->at("n70") -> Fill(3.1, evtwt);
    }

    // create regions
    bool signalRegion = (tau.getTightIsoMVA()  && muon.getIso() < 0.15);
    bool qcdRegion    = (tau.getMediumIsoMVA() && muon.getIso() < 0.30);
    bool wRegion      = (tau.getMediumIsoMVA() && muon.getIso() < 0.30);
    bool wsfRegion    = (tau.getTightIsoMVA()  && muon.getIso() < 0.15);
    bool qcdCR        = (tau.getTightIsoMVA()  && muon.getIso() > 0.15 && muon.getIso() < 0.30);

    // create categories
    bool zeroJet = (jets.getNjets() == 0);
    bool boosted = (jets.getNjets() == 1 || (jets.getNjets() > 1 && 
                   (jets.getDijetMass() <= 300 || Higgs.Pt() <= 50 || tau.getPt() <= 40)));
    bool vbfCat  = (jets.getNjets() > 1 && Higgs.Pt() > 50 && jets.getDijetMass() > 300 && tau.getPt() > 40);
    bool VHCat   = (jets.getNjets() > 1 && jets.getDijetMass() < 300);

    histos->at("pre_mt") -> Fill(mt, 1.);
    histos->at("pre_tau_pt") -> Fill(tau.getPt(), 1.);
    histos->at("pre_tau_iso") -> Fill(tau.getTightIsoMVA(), 1.);
    histos->at("pre_mu_iso") -> Fill(muon.getIso(), 1.);

    if (mt < 50 && tau.getPt() > 30) {

      // event categorizaation
      if (zeroJet) {

        if (signalRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h0_OS") -> Fill(tau.getL2DecayMode(), (muon.getP4() + tau.getP4()).M(), evtwt);
          } else {
            histos_2d->at("h0_SS") -> Fill(tau.getL2DecayMode(), (muon.getP4() + tau.getP4()).M(), evtwt);
          }
        } // close if signal block

        if (qcdRegion) {
          histos_2d->at("h0_QCD") -> Fill(tau.getL2DecayMode(), (muon.getP4() + tau.getP4()).M(), evtwt);
        } // close if qcd block

        if (wRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h0_WOS") -> Fill(tau.getL2DecayMode(), (muon.getP4() + tau.getP4()).M(), evtwt);
          } else {
            histos_2d->at("h0_WSS") -> Fill(tau.getL2DecayMode(), (muon.getP4() + tau.getP4()).M(), evtwt);
          }
        } // close if W block

      } else if (boosted) {

        if (signalRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h1_OS") -> Fill(Higgs.Pt(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h1_SS") -> Fill(Higgs.Pt(), event.getMSV(), evtwt);
          }
        } // close if signal block

        if (qcdRegion) {
          histos_2d->at("h1_QCD") -> Fill(Higgs.Pt(), event.getMSV(), evtwt);
        } // close if qcd block

        if (wRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h1_WOS") -> Fill(Higgs.Pt(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h1_WSS") -> Fill(Higgs.Pt(), event.getMSV(), evtwt);
          }
        } // close if W block

      } else if (vbfCat) {

        if (signalRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h2_OS") -> Fill(jets.getDijetMass(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h2_SS") -> Fill(jets.getDijetMass(), event.getMSV(), evtwt);
          }
        } // close if signal block

        if (qcdRegion) {
          histos_2d->at("h2_QCD") -> Fill(jets.getDijetMass(), event.getMSV(), evtwt);
        } // close if qcd block

        if (wRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h2_WOS") -> Fill(jets.getDijetMass(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h2_WSS") -> Fill(jets.getDijetMass(), event.getMSV(), evtwt);
          }
        } // close if W block

      } else if (VHCat) {

        if (signalRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h3_OS") -> Fill(tau.getPt(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h3_SS") -> Fill(tau.getPt(), event.getMSV(), evtwt);
          }
        } // close if signal block

        if (qcdRegion) {
          histos_2d->at("h3_QCD") -> Fill(tau.getPt(), event.getMSV(), evtwt);
        } // close if qcd block

        if (wRegion) {
          if (evt_charge == 0) {
            histos_2d->at("h3_WOS") -> Fill(tau.getPt(), event.getMSV(), evtwt);
          } else {
            histos_2d->at("h3_WSS") -> Fill(tau.getPt(), event.getMSV(), evtwt);
          }
        } // close if W block

      } // close VH

      histos->at("cutflow")->Fill(7., 1.);
      // inclusive selection
      if (signalRegion) {
        histos->at("cutflow")->Fill(8., 1.);

        if (evt_charge == 0) {
          // fill histograms
          histos->at("cutflow")->Fill(9., 1.);
          if (helper.deltaR(muon.getEta(), muon.getPhi(), tau.getEta(), tau.getPhi()) > 0.5) {
            histos->at("cutflow")->Fill(10., 1.);
            histos->at("hmu_pt")->Fill(muon.getPt(), evtwt);
            histos->at("hmu_eta")->Fill(muon.getEta(), evtwt);
            histos->at("hmu_phi")->Fill(muon.getPhi(), evtwt);
            histos->at("htau_pt")->Fill(tau.getPt(), evtwt);
            histos->at("htau_eta")->Fill(tau.getEta(), evtwt);
            histos->at("htau_phi")->Fill(tau.getPhi(), evtwt);
            histos->at("hmet")->Fill(met.getMet(), evtwt);
            histos->at("hmet_x")->Fill(met_x, evtwt);
            histos->at("hmet_y")->Fill(met_y, evtwt);
            histos->at("hmet_pt")->Fill(met_pt, evtwt);
            histos->at("hmt")->Fill(mt, evtwt);
            histos->at("hnjets")->Fill(jets.getNjets(), evtwt);
            histos->at("hmjj")->Fill(jets.getDijetMass(), evtwt);
            histos->at("hNGenJets")->Fill(event.getNumGenJets(), evtwt);
            histos->at("pt_sv")->Fill(event.getPtSV() ,evtwt);
            histos->at("m_sv")->Fill(event.getMSV(), evtwt);
            histos->at("Dbkg_VBF")->Fill(event.getDbkg_VBF(), evtwt);
            histos->at("Phi")->Fill(event.getPhi(), evtwt);
            histos->at("Phi1")->Fill(event.getPhi1(), evtwt);
            histos->at("Q2V1")->Fill(event.getQ2V1(), evtwt);
            histos->at("Q2V2")->Fill(event.getQ2V2(), evtwt);
            histos->at("costheta1")->Fill(event.getCosTheta1(), evtwt);
            histos->at("costheta2")->Fill(event.getCosTheta2(), evtwt);
            histos->at("costhetastar")->Fill(event.getCosThetaStar(), evtwt);
          }
        } else {
          histos->at("htau_pt_SS")->Fill(tau.getPt(), evtwt);
          histos->at("hmu_pt_SS")->Fill(muon.getPt(), evtwt);
          histos->at("htau_phi_SS")->Fill(tau.getPhi(), evtwt);
          histos->at("hmu_phi_SS")->Fill(muon.getPhi(), evtwt);
          histos->at("hmet_SS")->Fill(met.getMet(), evtwt);
          histos->at("hmt_SS")->Fill(mt, evtwt);
          histos->at("hmjj_SS")->Fill(jets.getDijetMass(), evtwt);
        }
      } // close signal
      if (qcdRegion) {
        histos->at("htau_pt_QCD")->Fill(tau.getPt(), evtwt);
        histos->at("hmu_pt_QCD")->Fill(muon.getPt(), evtwt);
        histos->at("htau_phi_QCD")->Fill(tau.getPhi(), evtwt);
        histos->at("hmu_phi_QCD")->Fill(muon.getPhi(), evtwt);
        histos->at("hmet_QCD")->Fill(met.getMet(), evtwt);
        histos->at("hmt_QCD")->Fill(mt, evtwt);
        histos->at("hmjj_QCD")->Fill(jets.getDijetMass(), evtwt);
      } // close qcd
      if (wRegion) {
        if (evt_charge == 0) {
          histos->at("htau_pt_WOS")->Fill(tau.getPt(), evtwt);
          histos->at("hmu_pt_WOS")->Fill(muon.getPt(), evtwt);
          histos->at("htau_phi_WOS")->Fill(tau.getPhi(), evtwt);
          histos->at("hmu_phi_WOS")->Fill(muon.getPhi(), evtwt);
          histos->at("hmet_WOS")->Fill(met.getMet(), evtwt);
          histos->at("hmt_WOS")->Fill(mt, evtwt);
          histos->at("hmjj_WOS")->Fill(jets.getDijetMass(), evtwt);
        } else {
          histos->at("htau_pt_WSS")->Fill(tau.getPt(), evtwt);
          histos->at("hmu_pt_WSS")->Fill(muon.getPt(), evtwt);
          histos->at("htau_phi_WSS")->Fill(tau.getPhi(), evtwt);
          histos->at("hmu_phi_WSS")->Fill(muon.getPhi(), evtwt);
          histos->at("hmet_WSS")->Fill(met.getMet(), evtwt);
          histos->at("hmt_WSS")->Fill(mt, evtwt);
          histos->at("hmjj_WSS")->Fill(jets.getDijetMass(), evtwt);
        } // close Wjets
      }   // close general

    } // close mt, tau selection

  } // close event loop
  histos->at("n70")->Fill(1, n70_count);
  histos->at("n70")->Write();

  fin->Close();
  fout->cd();
  fout->Write();
  fout->Close();
  return 0;
}
