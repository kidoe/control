package synth;
import java.io.IOException;

import javax.sound.midi.MidiDevice;
import javax.sound.midi.MidiMessage;
import javax.sound.midi.MidiUnavailableException;
import javax.sound.midi.Receiver;

import com.jsyn.JSyn;
import com.jsyn.Synthesizer;
import com.jsyn.devices.javasound.MidiDeviceTools;
import com.jsyn.midi.MessageParser;
import com.jsyn.midi.MidiConstants;
import com.jsyn.ports.UnitInputPort;
import com.jsyn.swing.DoubleBoundedRangeModel;
import com.jsyn.swing.ExponentialRangeModel;
import com.jsyn.swing.PortModelFactory;
import com.jsyn.unitgen.ContinuousRamp;
import com.jsyn.unitgen.LineOut;
import com.jsyn.unitgen.PowerOfTwo;
import com.jsyn.unitgen.UnitOscillator;
import com.jsyn.util.VoiceAllocator;
import com.softsynth.shared.time.TimeStamp;

import g4p_controls.G4P;
import g4p_controls.GCScheme;
import g4p_controls.GEvent;
import g4p_controls.GKnob;
import g4p_controls.GLabel;

import processing.core.PApplet;
public class synthMain{
	
	
	//Audio
	
	public static final int MAX_VOICES = 8; 
	public Synthesizer synth; 
	public VoiceAllocator allocator; 
	public LineOut lineOut; 
	public double vibratoRate = 5.0; 
	public double vibratoDepth = 0.0; 

	public UnitOscillator lfo; 
	public PowerOfTwo powerOfTwo; 
	public MessageParser messageParser; 
	public Voz1[] voices; 
	public ContinuousRamp rampFreqLFO1;
	ExponentialRangeModel amplitudeModelLFO = new ExponentialRangeModel("prueba",127,(double)0,(double)40,(double)0);
	ExponentialRangeModel amplitudeModelAmp = new ExponentialRangeModel("prueba",127,(double)0,(double)1,(double)0);
	ExponentialRangeModel amplitudeModelAttack = new ExponentialRangeModel("prueba",127,(double)0,(double)8,(double)0);
	ExponentialRangeModel amplitudeModelFilterFreq = new ExponentialRangeModel("prueba",127,(double)0,(double)6000,(double)0);

	
	// Need G4P library

/*	public static void main(String[] args) {
		// TODO Auto-generated method stub
//	PApplet.main(synthMain.class);
	synthMain main = new synthMain();

	try {
		main.test();
	} catch (MidiUnavailableException | IOException | InterruptedException e) {
		// TODO Auto-generated catch block
		e.printStackTrace();
	}
		
		
		}*/
	
		public void setupSynth() 
		{ 
			
		 synth = JSyn.createSynthesizer(); 

		 // Add an output. 
		 synth.add( lineOut = new LineOut() ); 
		 
		 voices = new Voz1[MAX_VOICES]; 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
		  Voz1 voice = new Voz1(); 
		  synth.add( voice );
		  voice.getOutput().connect( 0, lineOut.input, 0 ); 
		  voice.getOutput().connect( 0, lineOut.input, 1 ); 
		  voices[i] = voice; 
		 } 
		 allocator = new VoiceAllocator( voices ); 

		 // Start synthesizer using default stereo output at 44100 Hz. 
		 synth.start(); 
		 // We only need to start the LineOut. It will pull data from the 
		 // oscillator. 
		 lineOut.start(); 

		 // Get synthesizer time in seconds. 
		 double timeNow = synth.getCurrentTime(); 

		 // Advance to a near future time so we have a clean start. 
		 double time = timeNow + 0.5; 
		 

		} 

		
		class CustomReceiver implements Receiver 
		{ 
		 public void close() 
		 { 
		  System.out.print( "Closed." ); 
		 } 

		 public void send( MidiMessage message, long timeStamp ) 
		 { 
		  byte[] bytes = message.getMessage(); 
		  messageParser.parse( bytes ); 
		 } 
		} 
		//Crear clase Message

		class MyParser extends MessageParser 
		{ 
		 @Override 
		 public void controlChange( int channel, int index, int value ) 
		 { 
			 
			 System.out.println("Midi:" + value);
			 System.out.println("index:" + index);
			 switch (index) {
			 case 1:setFreqLFO1(value);	
			 
			 	 break;	
			 case 2:setFreqLFO2(value);	 	 
			 	 break;
			 case 3:setFreqLFO3(value);
				 break;
			 case 4:setFreqLFO4(value);
				 break;
			 case 5:setFilterFreq(value);
				 break;
			 case 6:
				 break;
			 case 7:setFreqLFOSineMod(value);
				 break;
			 case 8:
				 setSineAttack(value);
				 break;
			 case 9:setAmpLFO1(value);
			 	 break;
			 case 10:setAmpLFO2(value);
			 	 break;
			 case 11:setAmpLFO3(value);
				 break;
			 case 12:setAmpLFO4(value); 
				 break;
			 case 13:
				 break;
			 case 14:
				 break;
			 case 15:
				 break;
			 case 16:
				 break;
			 case 17:setFilterRes(value);
				 break;
				 
			}
			
		 }
		 public Voz1[] getVoice(){
			 return voices;
		 }

	 public void noteOff( int channel, int noteNumber, int velocity ) 
	 { 
	  allocator.noteOff( noteNumber, synth.createTimeStamp() ); 
	 } 

	 public void noteOn( int channel, int noteNumber, int velocity ) 
	 { 
	  double frequency = convertPitchToFrequency( noteNumber ); 
	  double amplitude = velocity / (4 * 128.0); 
	  TimeStamp timeStamp = synth.createTimeStamp(); 
	  allocator.noteOn( noteNumber, frequency, amplitude, timeStamp  ); 
	 } 

	 public void pitchBend( int channel, int bend ) 
	 { 
	  double fraction = (bend - MidiConstants.PITCH_BEND_CENTER) / ((double)MidiConstants.PITCH_BEND_CENTER); 
	  System.out.println( "bend = " + bend + ", fraction = " + fraction ); 
	 } 
	 }
		
	 double convertPitchToFrequency( double pitch ) 
	 { 
	  final double concertA = 440.0; 
	  return concertA * Math.pow( 2.0, ((pitch - 69) * (1.0 / 12.0)) ); 
	 } 
	 
	 public int test() throws MidiUnavailableException, IOException, 
	 InterruptedException 
	 { 
			
	 	setupSynth(); 

	 	messageParser = new MyParser(); 

	 	int result = 2; 
	 	// I know that my keyboard has "Usb in the description". 
	 	// There is another device that does not have Usb that I want to avoid. 
	 	MidiDevice keyboard = MidiDeviceTools.findKeyboard( "" ); 
	 	Receiver receiver = new CustomReceiver(); 
	 	// Just use default synthesizer. 
	 	if( keyboard != null ) 
	 	{ 
	 			// If you forget to open them you will hear no sound. 
	 			keyboard.open(); 
	 			// Put the receiver in the transmitter. 
	 			// This gives fairly low latency playing. 
	 			keyboard.getTransmitter().setReceiver( receiver ); 
	 			System.out.println( "Play MIDI keyboard: " + keyboard.getDeviceInfo().getDescription() ); 
	 			result = 0; 
	 	} 
	 	else 
	 	{ 
	 		System.out.println( "Could not find a keyboard." ); 
	 	} 
	 	return result; 
	 } 
	 public Voz1[] getFLO(){
		 return  voices;
	 }
	 
	 public void setFreqLFO1(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc.frequency.set(amplitudeModelLFO.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc.frequency.get());
		 
	 }
	 public void setFreqLFO2(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc2.frequency.set(amplitudeModelLFO.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc2.frequency.get());
		 
	 }
	 public void setFreqLFO3(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc3.frequency.set(amplitudeModelLFO.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc3.frequency.get());
		 
	 }
	 public void setFreqLFO4(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc4.frequency.set(amplitudeModelLFO.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc4.frequency.get());
		 
	 }
	
	 public void setAmpLFO1(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc.amplitude.get());
	 }
	 public void setAmpLFO2(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc2.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc2.amplitude.get());
	 }
	 public void setAmpLFO3(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc3.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc3.amplitude.get());
	 }
	 public void setAmpLFO4(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc4.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc4.amplitude.get());
	 }
	 public void setAmpSine(int value){
		
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 if(value == 0){
				 voices[i].mSineOscPM.amplitude.set(0);
			 }else{
				 voices[i].mSineOscPM.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
			 }
		 }
//		 System.out.println(voices[0].mSineOscPM.amplitude.get());
	 }
	 public void setSineAttack(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mDAHDSR.attack.set(amplitudeModelAttack.sliderToDouble(value));
		 }
		 
	 }
	 public void setSineDecay(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mDAHDSR.decay.set(amplitudeModelAttack.sliderToDouble(value));
		 }
		 
	 }
	 public void setSineSustaine(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mDAHDSR.sustain.set(amplitudeModelAttack.sliderToDouble(value));
		 }
		 
	 }
	 public void setSineRelease(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mDAHDSR.release.set(amplitudeModelAttack.sliderToDouble(value));
		 }
		 
	 }
	 
	 public void setAmpSaw(int value){
			
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 if(value == 0){
				 voices[i].mSawOsc.amplitude.set(0);
			 }else{
				 voices[i].mSawOsc.amplitude.set(amplitudeModelAmp.sliderToDouble(value));
			 }
		 }
		 System.out.println(voices[0].mSawOsc.amplitude.get());
	 }
	 
	 public void setFilterFreq(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mLowPass.frequency.set(amplitudeModelFilterFreq.sliderToDouble(value));
		 }
		 
	 }
	 public void setFilterRes(int value){
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mLowPass.Q.set(amplitudeModelFilterFreq.sliderToDouble(value));
		 }
		 
	 }
	 
	 public void setFreqLFOSineMod(int value){
		 
		 for( int i = 0; i < MAX_VOICES; i++ ) 
		 { 
			 voices[i].mSineOsc5.frequency.set(amplitudeModelLFO.sliderToDouble(value));
		 }
		 System.out.println(voices[0].mSineOsc5.frequency.get());
		 
	 }
	 
	 
	 
}
	
		
	
