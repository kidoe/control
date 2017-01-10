package synth;

import java.io.IOException;

import javax.sound.midi.MidiUnavailableException;

import com.jsyn.swing.ExponentialRangeModel;

import g4p_controls.G4P;
import g4p_controls.GCScheme;
import g4p_controls.GEvent;
import g4p_controls.GKnob;
import g4p_controls.GLabel;

import processing.core.PApplet;

public class view extends PApplet {
	
	GKnob kFilterCutoff; 
	GKnob kFilterRes; 
	GKnob kAmpLFO3; 
	GKnob kAmpLFO4; 
	GLabel label1; 
	GLabel label2; 
	GLabel label3; 
	GLabel label4; 
	GLabel label5; 
	GKnob kAmpLFO1; 
	GKnob kAmpLFO2; 
	GKnob kFreLFO1; 
	GKnob kFreLFO2; 
	GKnob kFreLFO3; 
	GKnob kFreLFO4; 
	GLabel label6; 
	GLabel label7; 
	GLabel label8; 
	GLabel label9; 
	GLabel label10; 
	GLabel label11; 
	GLabel label12; 
	GLabel label13; 
	GKnob kAmpSine; 
	GKnob kAmpSaw; 
	GKnob kAmpTri; 
	GKnob kAmpRNoise; 
	GLabel label14; 
	GLabel label15; 
	GLabel label16; 
	GLabel label17; 
	GLabel label18; 
	GKnob kSineModLFO; 
	GLabel label19; 
	GLabel label20; 
	GLabel label21; 
	GLabel label22; 
	GLabel label24; 
	GLabel label25; 
	GKnob kSawAttack; 
	GKnob kSawDecay; 
	GKnob kSawSustaine; 
	GKnob kSawRelease; 
	GKnob kSineRelease; 
	GKnob kSineSustaine; 
	GKnob kSineDecay; 
	GKnob kSineAttack; 
	GLabel label26; 
	GLabel label27; 
	GLabel label28; 
	GKnob kSquareAttack; 
	GKnob kSquareDecay; 
	GKnob kSquareSustaine; 
	GKnob kSquareRelease; 
	GKnob kNoiseAttack; 
	GKnob kNoiseRelease; 
	GKnob kNoiseSustaine; 
	GKnob kNoiseDecay; 
	static Voz1 [] v1;
	static synthMain main;
	static sinewaveView view1;
	ExponentialRangeModel amplitudeModelLFO = new ExponentialRangeModel("prueba",127,(double)0,(double)40,(double)0);
	ExponentialRangeModel amplitudeModelAmp = new ExponentialRangeModel("prueba",127,(double)0,(double)1,(double)0);

	
	
	public static void main(String[] args) {
	
	PApplet.main(view.class);

	
	main = new synthMain();
	
	String[] openLauncher = {
			"prueba",
	};
	view1 = new sinewaveView();
	PApplet.runSketch(openLauncher,view1);
	
	
	
	try {
		main.test();
		v1 = main.getFLO();
	} catch (MidiUnavailableException | IOException | InterruptedException e) {
		// TODO Auto-generated catch block
		e.printStackTrace();
	}
	
	
		}
	public void settings(){
	       size(1024,768);

	        
	    }
	
		
		public void setup(){
			createGUI();
		     
		}
		public void draw(){
		  background(230);
		 
		  if(v1 != null){
			  for (int i = 0 ; i < v1.length ; i++){
				  kFreLFO1.setValue((float)amplitudeModelLFO.doubleToSlider(v1[i].mSineOsc.frequency.getValue()));
//				  kAmpSine.setValue((float)amplitudeModelAmp.doubleToSlider(v1[i].mSineOscPM.amplitude.getValue()));
				  kAmpLFO1.setValue((float)amplitudeModelAmp.doubleToSlider(v1[i].mSineOsc.amplitude.getValue()));
			  }
			
		  }
		  
		  
		}
		
		public void createGUI(){

			  G4P.messagesEnabled(false);
			  G4P.setGlobalColorScheme(GCScheme.BLUE_SCHEME);
			  G4P.setCursor(ARROW);
			//  surface.setTitle("Sketch Window");
			  kFilterCutoff = new GKnob(this, 350, 500, 60, 60, (float) 0.8);
			  kFilterCutoff.setTurnRange(110, 70);
			  kFilterCutoff.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFilterCutoff.setSensitivity(1);
			  kFilterCutoff.setShowArcOnly(false);
			  kFilterCutoff.setOverArcOnly(false);
			  kFilterCutoff.setIncludeOverBezel(false);
			  kFilterCutoff.setShowTrack(true);
			  kFilterCutoff.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFilterCutoff.setShowTicks(true);
			  kFilterCutoff.setOpaque(false);
			  kFilterCutoff.addEventHandler(this, "kFilterCutoff_turn1");
			  kFilterRes = new GKnob(this, 350, 640, 60, 60, (float) 0.8);
			  kFilterRes.setTurnRange(110, 70);
			  kFilterRes.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFilterRes.setSensitivity(1);
			  kFilterRes.setShowArcOnly(false);
			  kFilterRes.setOverArcOnly(false);
			  kFilterRes.setIncludeOverBezel(false);
			  kFilterRes.setShowTrack(true);
			  kFilterRes.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFilterRes.setShowTicks(true);
			  kFilterRes.setOpaque(false);
			  kFilterRes.addEventHandler(this, "kFilterRes_turn1");
			  kAmpLFO3 = new GKnob(this, 160, 640, 60, 60,(float) 0.8);
			  kAmpLFO3.setTurnRange(110, 70);
			  kAmpLFO3.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpLFO3.setSensitivity(1);
			  kAmpLFO3.setShowArcOnly(false);
			  kAmpLFO3.setOverArcOnly(false);
			  kAmpLFO3.setIncludeOverBezel(false);
			  kAmpLFO3.setShowTrack(true);
			  kAmpLFO3.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpLFO3.setShowTicks(true);
			  kAmpLFO3.setOpaque(false);
			  kAmpLFO3.addEventHandler(this, "kAmpLFO3_turn1");
			  kAmpLFO4 = new GKnob(this, 230, 640, 60, 60, (float)0.8);
			  kAmpLFO4.setTurnRange(110, 70);
			  kAmpLFO4.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpLFO4.setSensitivity(1);
			  kAmpLFO4.setShowArcOnly(false);
			  kAmpLFO4.setOverArcOnly(false);
			  kAmpLFO4.setIncludeOverBezel(false);
			  kAmpLFO4.setShowTrack(true);
			  kAmpLFO4.setLimits((float)0.0, (float)0.0,(float) 127.0);
			  kAmpLFO4.setShowTicks(true);
			  kAmpLFO4.setOpaque(false);
			  kAmpLFO4.addEventHandler(this, "kAmpLFO4_turn1");
			  label1 = new GLabel(this, 10, 600, 80, 20);
			  label1.setText("Amp LFO1");
			  label1.setOpaque(false);
			  label2 = new GLabel(this, 80, 600, 80, 20);
			  label2.setText("Ampv LFO2");
			  label2.setOpaque(false);
			  label3 = new GLabel(this, 150, 600, 80, 20);
			  label3.setText("Amp LFO3");
			  label3.setOpaque(false);
			  label4 = new GLabel(this, 220, 600, 80, 20);
			  label4.setText("Amp LFO4");
			  label4.setOpaque(false);
			  label5 = new GLabel(this, 120, 420, 80, 20);
			  label5.setText("LFOs");
			  label5.setOpaque(false);
			  kAmpLFO1 = new GKnob(this, 20, 640, 60, 60, (float)0.8);
			  kAmpLFO1.setTurnRange(110, 70);
			  kAmpLFO1.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpLFO1.setSensitivity(1);
			  kAmpLFO1.setShowArcOnly(false);
			  kAmpLFO1.setOverArcOnly(false);
			  kAmpLFO1.setIncludeOverBezel(false);
			  kAmpLFO1.setShowTrack(true);
			  kAmpLFO1.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpLFO1.setShowTicks(true);
			  kAmpLFO1.setOpaque(false);
			  kAmpLFO1.addEventHandler(this, "kAmpLFO1_turn1");
			  kAmpLFO2 = new GKnob(this, 90, 640, 60, 60, (float)0.8);
			  kAmpLFO2.setTurnRange(110, 70);
			  kAmpLFO2.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpLFO2.setSensitivity(1);
			  kAmpLFO2.setShowArcOnly(false);
			  kAmpLFO2.setOverArcOnly(false);
			  kAmpLFO2.setIncludeOverBezel(false);
			  kAmpLFO2.setShowTrack(true);
			  kAmpLFO2.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpLFO2.setShowTicks(true);
			  kAmpLFO2.setOpaque(false);
			  kAmpLFO2.addEventHandler(this, "kAmpLFO2_turn1");
			  kFreLFO1 = new GKnob(this, 20, 500, 60, 60,(float) 0.8);
			  kFreLFO1.setTurnRange(110, 70);
			  kFreLFO1.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFreLFO1.setSensitivity(1);
			  kFreLFO1.setShowArcOnly(false);
			  kFreLFO1.setOverArcOnly(false);
			  kFreLFO1.setIncludeOverBezel(false);
			  kFreLFO1.setShowTrack(true);
			  kFreLFO1.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFreLFO1.setShowTicks(true);
			  kFreLFO1.setOpaque(false);
			  kFreLFO1.addEventHandler(this, "kFreLFO1_turn2");
			  kFreLFO2 = new GKnob(this, 90, 500, 60, 60, (float)0.8);
			  kFreLFO2.setTurnRange(110, 70);
			  kFreLFO2.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFreLFO2.setSensitivity(1);
			  kFreLFO2.setShowArcOnly(false);
			  kFreLFO2.setOverArcOnly(false);
			  kFreLFO2.setIncludeOverBezel(false);
			  kFreLFO2.setShowTrack(true);
			  kFreLFO2.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFreLFO2.setShowTicks(true);
			  kFreLFO2.setOpaque(false);
			  kFreLFO2.addEventHandler(this, "kFreLFO2_turn1");
			  kFreLFO3 = new GKnob(this, 160, 500, 60, 60,(float) 0.8);
			  kFreLFO3.setTurnRange(110, 70);
			  kFreLFO3.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFreLFO3.setSensitivity(1);
			  kFreLFO3.setShowArcOnly(false);
			  kFreLFO3.setOverArcOnly(false);
			  kFreLFO3.setIncludeOverBezel(false);
			  kFreLFO3.setShowTrack(true);
			  kFreLFO3.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFreLFO3.setShowTicks(true);
			  kFreLFO3.setOpaque(false);
			  kFreLFO3.addEventHandler(this, "kFreLFO3_turn1");
			  kFreLFO4 = new GKnob(this, 230, 500, 60, 60, (float)0.8);
			  kFreLFO4.setTurnRange(110, 70);
			  kFreLFO4.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kFreLFO4.setSensitivity(1);
			  kFreLFO4.setShowArcOnly(false);
			  kFreLFO4.setOverArcOnly(false);
			  kFreLFO4.setIncludeOverBezel(false);
			  kFreLFO4.setShowTrack(true);
			  kFreLFO4.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kFreLFO4.setShowTicks(true);
			  kFreLFO4.setOpaque(false);
			  kFreLFO4.addEventHandler(this, "kFreLFO4_turn1");
			  label6 = new GLabel(this, 10, 460, 80, 20);
			  label6.setText("Freq Sine");
			  label6.setOpaque(false);
			  label7 = new GLabel(this, 220, 460, 80, 20);
			  label7.setText("Freq LFO4");
			  label7.setOpaque(false);
			  label8 = new GLabel(this, 80, 460, 80, 20);
			  label8.setText("Freq LFO2");
			  label8.setOpaque(false);
			  label9 = new GLabel(this, 150, 460, 80, 20);
			  label9.setText("Freq LFO3");
			  label9.setOpaque(false);
			  label10 = new GLabel(this, 340, 420, 80, 20);
			  label10.setText("Filter LP");
			  label10.setOpaque(false);
			  label11 = new GLabel(this, 340, 460, 80, 20);
			  label11.setText("Freq");
			  label11.setOpaque(false);
			  label12 = new GLabel(this, 340, 600, 80, 20);
			  label12.setText("Res");
			  label12.setOpaque(false);
			  label13 = new GLabel(this, 560, 420, 80, 20);
			  label13.setText("OSCs");
			  label13.setOpaque(false);
			  kAmpSine = new GKnob(this, 470, 500, 60, 60,(float) 0.8);
			  kAmpSine.setTurnRange(110, 70);
			  kAmpSine.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpSine.setSensitivity(1);
			  kAmpSine.setShowArcOnly(false);
			  kAmpSine.setOverArcOnly(false);
			  kAmpSine.setIncludeOverBezel(false);
			  kAmpSine.setShowTrack(true);
			  kAmpSine.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpSine.setShowTicks(true);
			  kAmpSine.setOpaque(false);
			  kAmpSine.addEventHandler(this, "kAmpSine_turn1");
			  kAmpSaw = new GKnob(this, 540, 500, 60, 60, (float)0.8);
			  kAmpSaw.setTurnRange(110, 70);
			  kAmpSaw.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpSaw.setSensitivity(1);
			  kAmpSaw.setShowArcOnly(false);
			  kAmpSaw.setOverArcOnly(false);
			  kAmpSaw.setIncludeOverBezel(false);
			  kAmpSaw.setShowTrack(true);
			  kAmpSaw.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpSaw.setShowTicks(true);
			  kAmpSaw.setOpaque(false);
			  kAmpSaw.addEventHandler(this, "kAmpSaw_turn1");
			  kAmpTri = new GKnob(this, 610, 500, 60, 60,(float)(float) 0.8);
			  kAmpTri.setTurnRange(110, 70);
			  kAmpTri.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpTri.setSensitivity(1);
			  kAmpTri.setShowArcOnly(false);
			  kAmpTri.setOverArcOnly(false);
			  kAmpTri.setIncludeOverBezel(false);
			  kAmpTri.setShowTrack(true);
			  kAmpTri.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpTri.setShowTicks(true);
			  kAmpTri.setOpaque(false);
			  kAmpTri.addEventHandler(this, "kAmpTri_turn1");
			  kAmpRNoise = new GKnob(this, 680, 500, 60, 60, (float)0.8);
			  kAmpRNoise.setTurnRange(110, 70);
			  kAmpRNoise.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kAmpRNoise.setSensitivity(1);
			  kAmpRNoise.setShowArcOnly(false);
			  kAmpRNoise.setOverArcOnly(false);
			  kAmpRNoise.setIncludeOverBezel(false);
			  kAmpRNoise.setShowTrack(true);
			  kAmpRNoise.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kAmpRNoise.setShowTicks(true);
			  kAmpRNoise.setOpaque(false);
			  kAmpRNoise.addEventHandler(this, "kAmpRNoise_turn1");
			  label14 = new GLabel(this, 70, 180, 80, 20);
			  label14.setText("Attack");
			  label14.setOpaque(false);
			  label15 = new GLabel(this, 600, 460, 80, 20);
			  label15.setText("Square Amp");
			  label15.setOpaque(false);
			  label16 = new GLabel(this, 530, 460, 80, 20);
			  label16.setText("Saw Amp");
			  label16.setOpaque(false);
			  label17 = new GLabel(this, 460, 460, 80, 20);
			  label17.setText("Sine Amp");
			  label17.setOpaque(false);
			  label18 = new GLabel(this, 670, 460, 80, 20);
			  label18.setText("RNoise Amp");
			  label18.setOpaque(false);
			  kSineModLFO = new GKnob(this, 470, 640, 60, 60, (float)0.8);
			  kSineModLFO.setTurnRange(110, 70);
			  kSineModLFO.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSineModLFO.setSensitivity(1);
			  kSineModLFO.setShowArcOnly(false);
			  kSineModLFO.setOverArcOnly(false);
			  kSineModLFO.setIncludeOverBezel(false);
			  kSineModLFO.setShowTrack(true);
			  kSineModLFO.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSineModLFO.setShowTicks(true);
			  kSineModLFO.setOpaque(false);
			  kSineModLFO.addEventHandler(this, "kSineModLFO_turn1");
			  label19 = new GLabel(this, 460, 600, 80, 20);
			  label19.setText("Sine Mod LFO Freq");
			  label19.setOpaque(false);
			  label20 = new GLabel(this, 340, 80, 80, 20);
			  label20.setText("Envelopes");
			  label20.setOpaque(false);
			  label21 = new GLabel(this, 400, 120, 80, 20);
			  label21.setText("Square Env");
			  label21.setOpaque(false);
			  label22 = new GLabel(this, 280, 120, 80, 20);
			  label22.setText("Sine Env");
			  label22.setOpaque(false);
			  label24 = new GLabel(this, 170, 120, 80, 20);
			  label24.setText("Saw Env");
			  label24.setOpaque(false);
			  label25 = new GLabel(this, 510, 120, 80, 20);
			  label25.setText("Red Noise");
			  label25.setOpaque(false);
			  kSawAttack = new GKnob(this, 180, 160, 60, 60,(float) 0.8);
			  kSawAttack.setTurnRange(110, 70);
			  kSawAttack.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSawAttack.setSensitivity(1);
			  kSawAttack.setShowArcOnly(false);
			  kSawAttack.setOverArcOnly(false);
			  kSawAttack.setIncludeOverBezel(false);
			  kSawAttack.setShowTrack(true);
			  kSawAttack.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSawAttack.setShowTicks(true);
			  kSawAttack.setOpaque(false);
			  kSawAttack.addEventHandler(this, "kSawAttack_turn1");
			  kSawDecay = new GKnob(this, 180, 230, 60, 60,(float) 0.8);
			  kSawDecay.setTurnRange(110, 70);
			  kSawDecay.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSawDecay.setSensitivity(1);
			  kSawDecay.setShowArcOnly(false);
			  kSawDecay.setOverArcOnly(false);
			  kSawDecay.setIncludeOverBezel(false);
			  kSawDecay.setShowTrack(true);
			  kSawDecay.setLimits((float)0.5, (float)0.0, (float)8.0);
			  kSawDecay.setShowTicks(true);
			  kSawDecay.setOpaque(false);
			  kSawDecay.addEventHandler(this, "kSawDecay_turn1");
			  kSawSustaine = new GKnob(this, 180, 300, 60, 60,(float) 0.8);
			  kSawSustaine.setTurnRange(110, 70);
			  kSawSustaine.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSawSustaine.setSensitivity(1);
			  kSawSustaine.setShowArcOnly(false);
			  kSawSustaine.setOverArcOnly(false);
			  kSawSustaine.setIncludeOverBezel(false);
			  kSawSustaine.setShowTrack(true);
			  kSawSustaine.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSawSustaine.setShowTicks(true);
			  kSawSustaine.setOpaque(false);
			  kSawSustaine.addEventHandler(this, "kSawSustaine_turn1");
			  kSawRelease = new GKnob(this, 180, 370, 60, 60,(float) 0.8);
			  kSawRelease.setTurnRange(110, 70);
			  kSawRelease.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSawRelease.setSensitivity(1);
			  kSawRelease.setShowArcOnly(false);
			  kSawRelease.setOverArcOnly(false);
			  kSawRelease.setIncludeOverBezel(false);
			  kSawRelease.setShowTrack(true);
			  kSawRelease.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSawRelease.setShowTicks(true);
			  kSawRelease.setOpaque(false);
			  kSawRelease.addEventHandler(this, "kSawRelease_turn1");
			  kSineRelease = new GKnob(this, 290, 370, 60, 60, (float)0.8);
			  kSineRelease.setTurnRange(110, 70);
			  kSineRelease.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSineRelease.setSensitivity(1);
			  kSineRelease.setShowArcOnly(false);
			  kSineRelease.setOverArcOnly(false);
			  kSineRelease.setIncludeOverBezel(false);
			  kSineRelease.setShowTrack(true);
			  kSineRelease.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSineRelease.setShowTicks(true);
			  kSineRelease.setOpaque(false);
			  kSineRelease.addEventHandler(this, "kSineRelease_turn1");
			  kSineSustaine = new GKnob(this, 290, 300, 60, 60,(float) 0.8);
			  kSineSustaine.setTurnRange(110, 70);
			  kSineSustaine.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSineSustaine.setSensitivity(1);
			  kSineSustaine.setShowArcOnly(false);
			  kSineSustaine.setOverArcOnly(false);
			  kSineSustaine.setIncludeOverBezel(false);
			  kSineSustaine.setShowTrack(true);
			  kSineSustaine.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSineSustaine.setShowTicks(true);
			  kSineSustaine.setOpaque(false);
			  kSineSustaine.addEventHandler(this, "kSineSustaine_turn1");
			  kSineDecay = new GKnob(this, 290, 230, 60, 60,(float) 0.8);
			  kSineDecay.setTurnRange(110, 70);
			  kSineDecay.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSineDecay.setSensitivity(1);
			  kSineDecay.setShowArcOnly(false);
			  kSineDecay.setOverArcOnly(false);
			  kSineDecay.setIncludeOverBezel(false);
			  kSineDecay.setShowTrack(true);
			  kSineDecay.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSineDecay.setShowTicks(true);
			  kSineDecay.setOpaque(false);
			  kSineDecay.addEventHandler(this, "kSineDecay_turn1");
			  kSineAttack = new GKnob(this, 290, 160, 60, 60,(float) 0.8);
			  kSineAttack.setTurnRange(110, 70);
			  kSineAttack.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSineAttack.setSensitivity(1);
			  kSineAttack.setShowArcOnly(false);
			  kSineAttack.setOverArcOnly(false);
			  kSineAttack.setIncludeOverBezel(false);
			  kSineAttack.setShowTrack(true);
			  kSineAttack.setLimits((float)0.0,(float) 0.0,(float) 127.0);
			  kSineAttack.setShowTicks(true);
			  kSineAttack.setOpaque(false);
			  kSineAttack.addEventHandler(this, "kSineAttack_turn1");
			  label26 = new GLabel(this, 70, 250, 80, (float)20);
			  label26.setText("Decay");
			  label26.setOpaque(false);
			  label27 = new GLabel(this, 70, 320, 80, 20);
			  label27.setText("Sustaine");
			  label27.setOpaque(false);
			  label28 = new GLabel(this, 70, 390, 80, 20);
			  label28.setText("Release");
			  label28.setOpaque(false);
			  kSquareAttack = new GKnob(this, 410, 160, 60, 60, (float)0.8);
			  kSquareAttack.setTurnRange(110, 70);
			  kSquareAttack.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSquareAttack.setSensitivity(1);
			  kSquareAttack.setShowArcOnly(false);
			  kSquareAttack.setOverArcOnly(false);
			  kSquareAttack.setIncludeOverBezel(false);
			  kSquareAttack.setShowTrack(true);
			  kSquareAttack.setLimits((float)0.5,(float) 0.0,(float) 127.0);
			  kSquareAttack.setShowTicks(true);
			  kSquareAttack.setOpaque(false);
			  kSquareAttack.addEventHandler(this, "kSquareAttack_turn1");
			  kSquareDecay = new GKnob(this, 410, 230, 60, 60, (float)0.8);
			  kSquareDecay.setTurnRange(110, 70);
			  kSquareDecay.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSquareDecay.setSensitivity(1);
			  kSquareDecay.setShowArcOnly(false);
			  kSquareDecay.setOverArcOnly(false);
			  kSquareDecay.setIncludeOverBezel(false);
			  kSquareDecay.setShowTrack(true);
			  kSquareDecay.setLimits((float)0.5,(float) 0.0,(float) 127.0);
			  kSquareDecay.setShowTicks(true);
			  kSquareDecay.setOpaque(false);
			  kSquareDecay.addEventHandler(this, "kSquareDecay_turn1");
			  kSquareSustaine = new GKnob(this, 410, 300, 60, 60,(float) 0.8);
			  kSquareSustaine.setTurnRange(110, 70);
			  kSquareSustaine.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSquareSustaine.setSensitivity(1);
			  kSquareSustaine.setShowArcOnly(false);
			  kSquareSustaine.setOverArcOnly(false);
			  kSquareSustaine.setIncludeOverBezel(false);
			  kSquareSustaine.setShowTrack(true);
			  kSquareSustaine.setLimits((float)0.5, (float)0.0,(float) 127.0);
			  kSquareSustaine.setShowTicks(true);
			  kSquareSustaine.setOpaque(false);
			  kSquareSustaine.addEventHandler(this, "kSquareSustaine_turn1");
			  kSquareRelease = new GKnob(this, 410, 370, 60, 60,(float) 0.8);
			  kSquareRelease.setTurnRange(110, 70);
			  kSquareRelease.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kSquareRelease.setSensitivity(1);
			  kSquareRelease.setShowArcOnly(false);
			  kSquareRelease.setOverArcOnly(false);
			  kSquareRelease.setIncludeOverBezel(false);
			  kSquareRelease.setShowTrack(true);
			  kSquareRelease.setLimits((float)0.5, (float)0.0, (float)127.0);
			  kSquareRelease.setShowTicks(true);
			  kSquareRelease.setOpaque(false);
			  kSquareRelease.addEventHandler(this, "kSquareRelease_turn1");
			  kNoiseAttack = new GKnob(this, 520, 160, 60, 60,(float) 127.8);
			  kNoiseAttack.setTurnRange(110, 70);
			  kNoiseAttack.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kNoiseAttack.setSensitivity(1);
			  kNoiseAttack.setShowArcOnly(false);
			  kNoiseAttack.setOverArcOnly(false);
			  kNoiseAttack.setIncludeOverBezel(false);
			  kNoiseAttack.setShowTrack(true);
			  kNoiseAttack.setLimits((float)0.5, (float)0.0, (float)127.0);
			  kNoiseAttack.setShowTicks(true);
			  kNoiseAttack.setOpaque(false);
			  kNoiseAttack.addEventHandler(this, "kNoiseAttack_turn1");
			  kNoiseRelease = new GKnob(this, 520, 370, 60, 60, (float)0.8);
			  kNoiseRelease.setTurnRange(110, 70);
			  kNoiseRelease.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kNoiseRelease.setSensitivity(1);
			  kNoiseRelease.setShowArcOnly(false);
			  kNoiseRelease.setOverArcOnly(false);
			  kNoiseRelease.setIncludeOverBezel(false);
			  kNoiseRelease.setShowTrack(true);
			  kNoiseRelease.setLimits((float)0.5, (float)0.0,(float) 127.0);
			  kNoiseRelease.setShowTicks(true);
			  kNoiseRelease.setOpaque(false);
			  kNoiseRelease.addEventHandler(this, "kNoiseRelease_turn1");
			  kNoiseSustaine = new GKnob(this, 520, 300, 60, 60, (float)0.8);
			  kNoiseSustaine.setTurnRange(110, 70);
			  kNoiseSustaine.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kNoiseSustaine.setSensitivity(1);
			  kNoiseSustaine.setShowArcOnly(false);
			  kNoiseSustaine.setOverArcOnly(false);
			  kNoiseSustaine.setIncludeOverBezel(false);
			  kNoiseSustaine.setShowTrack(true);
			  kNoiseSustaine.setLimits((float)0.5,(float)0.0, (float)127.0);
			  kNoiseSustaine.setShowTicks(true);
			  kNoiseSustaine.setOpaque(false);
			  kNoiseSustaine.addEventHandler(this, "kNoiseSustaine_turn1");
			  kNoiseDecay = new GKnob(this, 520, 230, 60, 60,(float) 0.8);
			  kNoiseDecay.setTurnRange(110, 70);
			  kNoiseDecay.setTurnMode(GKnob.CTRL_HORIZONTAL);
			  kNoiseDecay.setSensitivity(1);
			  kNoiseDecay.setShowArcOnly(false);
			  kNoiseDecay.setOverArcOnly(false);
			  kNoiseDecay.setIncludeOverBezel(false);
			  kNoiseDecay.setShowTrack(true);
			  kNoiseDecay.setLimits((float)0.5,(float) 0.0, (float)127.0);
			  kNoiseDecay.setShowTicks(true);
			  kNoiseDecay.setOpaque(false);
			  kNoiseDecay.addEventHandler(this, "kNoiseDecay_turn1");
			}
		public void kFilterCutoff_turn1(GKnob source, GEvent event) { //_CODE_:kFilterCutoff:377788:
			  println("knob1 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kFilterCutoff:377788:

			public void kFilterRes_turn1(GKnob source, GEvent event) { //_CODE_:kFilterRes:776444:
			  println("knob2 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kFilterRes:776444:

			public void kAmpLFO3_turn1(GKnob source, GEvent event) { //_CODE_:kAmpLFO3:790680:
			  println("knob3 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpLFO3:790680:

			public void kAmpLFO4_turn1(GKnob source, GEvent event) { //_CODE_:kAmpLFO4:634480:
			  println("knob4 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpLFO4:634480:

			public void kAmpLFO1_turn1(GKnob source, GEvent event) { //_CODE_:kAmpLFO1:292545:
			  println("knob1 - GKnob >> GEvent." + event + " @ " + millis());
				main.setAmpLFO1(kAmpLFO1.getValueI());
			    view1.setAmplitude(kAmpLFO1.getValueF());
				
			} //_CODE_:kAmpLFO1:292545:

			public void kAmpLFO2_turn1(GKnob source, GEvent event) { //_CODE_:kAmpLFO2:429918:
			  println("knob2 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpLFO2:429918:

			public void kFreLFO1_turn2(GKnob source, GEvent event) { //_CODE_:kFreLFO1:975541:
				//setFreqLFO1(kFreLFO1.getValueI());
			
			  main.setFreqLFO1(kFreLFO1.getValueI()); 
			  view1.setFreq(kFreLFO1.getValueF());
			  System.out.println(kFreLFO1.getValueF());

			} //_CODE_:kFreLFO1:975541:

			public void kFreLFO2_turn1(GKnob source, GEvent event) { //_CODE_:kFreLFO2:824542:
			  println("knob5 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kFreLFO2:824542:

			public void kFreLFO3_turn1(GKnob source, GEvent event) { //_CODE_:kFreLFO3:419514:
			  println("knob6 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kFreLFO3:419514:

			public void kFreLFO4_turn1(GKnob source, GEvent event) { //_CODE_:kFreLFO4:846249:
			  println("knob7 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kFreLFO4:846249:

			public void kAmpSine_turn1(GKnob source, GEvent event) { //_CODE_:kAmpSine:318173:
			  
			  main.setAmpSine(kAmpSine.getValueI());
			 
			} //_CODE_:kAmpSine:318173:

			public void kAmpSaw_turn1(GKnob source, GEvent event) { //_CODE_:kAmpSaw:500270:
			  println("kAmpSaw - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpSaw:500270:

			public void kAmpTri_turn1(GKnob source, GEvent event) { //_CODE_:kAmpTri:695255:
			  println("kAmpTri - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpTri:695255:

			public void kAmpRNoise_turn1(GKnob source, GEvent event) { //_CODE_:kAmpRNoise:692555:
			  println("knob4 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kAmpRNoise:692555:

			public void kSineModLFO_turn1(GKnob source, GEvent event) { //_CODE_:kSineModLFO:931470:
			  println(main.voices[0].mSineOsc5.amplitude.get());
			  
			  view1.setTime((float) amplitudeModelAmp.sliderToDouble((int) kSineModLFO.getValueF()));
			} //_CODE_:kSineModLFO:931470:

			public void kSawAttack_turn1(GKnob source, GEvent event) { //_CODE_:kSawAttack:476871:
			  println("kSawAttack - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSawAttack:476871:

			public void kSawDecay_turn1(GKnob source, GEvent event) { //_CODE_:kSawDecay:789495:
			  println("knob2 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSawDecay:789495:

			public void kSawSustaine_turn1(GKnob source, GEvent event) { //_CODE_:kSawSustaine:810361:
			  println("knob3 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSawSustaine:810361:

			public void kSawRelease_turn1(GKnob source, GEvent event) { //_CODE_:kSawRelease:611046:
			  println("knob4 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSawRelease:611046:

			public void kSineRelease_turn1(GKnob source, GEvent event) { //_CODE_:kSineRelease:509769:
			  println("knob5 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSineRelease:509769:

			public void kSineSustaine_turn1(GKnob source, GEvent event) { //_CODE_:kSineSustaine:763310:
			  println("knob6 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSineSustaine:763310:

			public void kSineDecay_turn1(GKnob source, GEvent event) { //_CODE_:kSineDecay:790470:
			  println("knob7 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSineDecay:790470:

			public void kSineAttack_turn1(GKnob source, GEvent event) { //_CODE_:kSineAttack:325913:
			  println("knob8 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSineAttack:325913:

			public void kSquareAttack_turn1(GKnob source, GEvent event) { //_CODE_:kSquareAttack:404063:
			  println("knob9 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSquareAttack:404063:

			public void kSquareDecay_turn1(GKnob source, GEvent event) { //_CODE_:kSquareDecay:918872:
			  println("knob10 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSquareDecay:918872:

			public void kSquareSustaine_turn1(GKnob source, GEvent event) { //_CODE_:kSquareSustaine:640401:
			  println("knob11 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSquareSustaine:640401:

			public void kSquareRelease_turn1(GKnob source, GEvent event) { //_CODE_:kSquareRelease:643394:
			  println("knob12 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kSquareRelease:643394:

			public void kNoiseAttack_turn1(GKnob source, GEvent event) { //_CODE_:kNoiseAttack:245771:
			  println("knob13 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kNoiseAttack:245771:

			public void kNoiseRelease_turn1(GKnob source, GEvent event) { //_CODE_:kNoiseRelease:896056:
			  println("knob14 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kNoiseRelease:896056:

			public void kNoiseSustaine_turn1(GKnob source, GEvent event) { //_CODE_:kNoiseSustaine:840400:
			  println("knob15 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kNoiseSustaine:840400:

			public void kNoiseDecay_turn1(GKnob source, GEvent event) { //_CODE_:kNoiseDecay:513608:
			  println("knob16 - GKnob >> GEvent." + event + " @ " + millis());
			} //_CODE_:kNoiseDecay:513608:
}
