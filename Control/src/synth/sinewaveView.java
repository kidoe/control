package synth;

import java.io.IOException;

import javax.sound.midi.MidiUnavailableException;
import javax.xml.bind.annotation.XmlElementDecl.GLOBAL;

import processing.core.PApplet;

public class sinewaveView extends PApplet{
	int xspacing = 1;   // How far apart should each horizontal location be spaced
	int w;              // Width of entire wave

	float theta = (float) 0.0;  // Start angle at 0
	float amplitude = (float) 90.0;  // Height of wave
	float period = (float) 100.0;  // How many pixels before the wave repeats
	float dx;  // Value for incrementing X, a function of period and xspacing
	float[] yvalues;  // Using an array to store height values for the wave
	float time = (float) 0.02;
	

//	public static void main(String[] args) {
//		// TODO Auto-generated method stub
////	PApplet.main(sinewaveView.class);
//		}
	
	@Override
	public void setup() {
		// TODO Auto-generated method stub
		
		
		
	}
	@Override
	public void draw() {
		// TODO Auto-generated method stub
		dx = (TWO_PI / period) * xspacing;
		  background(0);
		  calcWave();
		  renderWave();
		  
	}

	@Override
	public void settings() {
		// TODO Auto-generated method stub
		  size(640, 360);
			w = width+16;
			dx = (TWO_PI / period) * xspacing;
			yvalues = new float[w/xspacing];
	}

	public void setAmplitude(float value){
		amplitude = value;
	}
	public void setFreq(float value){
		float newPeriod = interpol(1, 127,value) ;
		System.out.println(value);
		System.out.println(newPeriod);
		period =  newPeriod;
	}
	
	public float interpol(float v0,float v1,float t){
		return v0 + t*(v1-v0);
	}
	public void setTime(float value){
			time =+ value;	
	}

	void calcWave() {
		  // Increment theta (try different values for 'angular velocity' here
		  theta += time;

		  // For every x value, calculate a y value with sine function
		  float x = theta;
		  for (int i = 0; i < yvalues.length; i++) {
		    yvalues[i] = sin(x)*amplitude;
		    x+=dx;
		  }
		}

		void renderWave() {
		  noStroke();
		  fill(255);
		  // A simple way to draw the wave with an ellipse at each location
		  for (int x = 0; x < yvalues.length; x++) {
		    ellipse(x*xspacing, height/2+yvalues[x], 16, 16);
		  }
		}
}
