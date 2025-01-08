/*
The MIT License (MIT)

Copyright (c) 2016, Gameblabla

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


	var tapped = 0 , swipe_up = 0 , swipe_down = 0 , double_tap = 0 , swipe_down2 = 0 , swipe_up2 = 0;
	var poss_y = 0, poss_y_repere = 250 , follow_y = 0;
	var anim_back = 0;
	var total_sec = 0 , high_total_sec = 0;
	
	// Precalculation of the framerate
	var set_fps = 1000 / 30;


window.onload = function() {
		   var graph_bird = 0 , time_anim = 0;
		   var bird_x = 24, bird_y = 50;
		   var ready_y = 140;
		   var frotter = 0, game_mode = 0;
		   var touch_time = 0;
		   var lose_time = 0;
		   var keyboard_using = 0;
		   var second_0 , second_1, min_0 , min_1 , hour_0 , hour_1 , hour_2, micro_0 , micro_1;
		   var hour = 0 , min = 0 , second = 0 , micro = 0;
		   var size_x = 1.0 , size_y = 1.0;
		   
		   var h_h = localStorage.getItem("high-hour") || 0;
		   var h_m = localStorage.getItem ("high-min") || 0;
		   var h_s = localStorage.getItem ("high-second") || 0;
		   
		   var first_layer = document.getElementById('canvas');
		   var background = first_layer.getContext('2d'); // A layer for the background

		   var bird = document.images[0];
		   var back = document.images[1];
		   var logo = document.images[2];
		   var inst = document.images[3];
		   var ready = document.images[4];
		   var gu = document.images[5];
		   var go = document.images[6];
		   var sh = document.images[7];
		   var sco = document.images[8];
		   var point = document.images[9];

	   (function (window) {
	   
		function gameLoop() {

			/* This is used to clear the canvas
			and it's apparently faster than clear rect
			*/
			first_layer.height = first_layer.height;
			
			// Now , the background is drawn with CSS
			show_background();
			
			
			// If..else is faster than switch in Javascript
			
				if (game_mode == 0)
				{
					calcul_highscore();
					animate_bird();
					show_scale_bird_background();
					
					size_x = 1.0;
					size_y = 1.0;
					bird_x = 24; 
					bird_y = 50;

					background.drawImage(logo, 12, 20-8);
							
					highshowScore(2,140);

							
					if (tapped == 1 && touch_time > 4){
						ready_y = 140;
						game_mode = 1;
						tapped = 0;
						touch_time = 0;
						lose_time = 0;
						frotter = 0;
						swipe_down = 0;
						swipe_up = 0;
						swipe_down2 = 0;
						swipe_up2 = 0;
					}
							
					if (touch_time < 8) touch_time++;
				}
				else if (game_mode == 1)
				{
					animate_bird();
					show_bird_background();
								
					background.drawImage(inst, 12, 16-8);
					background.drawImage(ready, 32, ready_y-8);
								
					if (touch_time > 80) ready_y = ready_y + 3;
					if (touch_time < 95) touch_time++;
					else if (touch_time > 90){

						game_mode = 2;
						touch_time = 0;
						tapped = 0;
						hour = 0;
						min = 0;
						second = 0;
						micro = 0;
					}
				}				
				else if (game_mode == 2)
				{
					showScore();
					calcul_score();
					counter();
					
					show_bird_background();
					control_bird();
					animate_bird();		
				}
				else if (game_mode == 3)
				{
					show_scale_bird_background();
					highshowScore(0,136);
					conditions_gameover();
				}


	    }
			window.setInterval(gameLoop, set_fps); // 30fps
				
	} (window));

		function show_background(){
			background.drawImage(back, 0, anim_back*40, 30, 40, 0, 0, 120, 160);
			anim_back=anim_back+1;
			if (anim_back > 230) anim_back = 0;
		}	
	
		function show_scale_bird_background(){
			if (size_x > 0 && size_x < 1.0) background.save();
			if (size_x > 0 && size_x < 1.0) background.scale(size_x,size_y);
			background.drawImage(bird, 0, graph_bird*0, 72, 96, bird_x, bird_y-12, 72, 96);
			if (size_x > 0 && size_x < 1.0) background.restore();
		}
		
		function show_bird_background(){
			background.drawImage(bird, 0, graph_bird*96, 72, 96, bird_x, bird_y-8, 72, 96);	
		}
		
		function highshowScore(x,y){			
			background.drawImage(point, 43+x, 6+(y-4));
			background.drawImage(point, 68+x, 6+(y-4));
							
			background.drawImage(sco, second_0*12, 36, 12, 12, 72+x, y, 12, 12);
			background.drawImage(sco, second_1*12, 36, 12, 12, 82+x, y, 12, 12);

			background.drawImage(sco, min_0*12, 12, 12, 12, 46+x, y, 12, 12);
			background.drawImage(sco, min_1*12, 12, 12, 12, 56+x, y, 12, 12);

			background.drawImage(sco, hour_0*12, 24, 12, 12, 20+x, y, 12, 12);
			background.drawImage(sco, hour_1*12, 24, 12, 12, 30+x, y, 12, 12);
			
		}
	
		function showScore(){
			background.drawImage(point, 43, 6);
			background.drawImage(point, 68, 6);
							
			background.drawImage(sco, second_0*12, 36, 12, 12, 72, 5, 12, 12);
			background.drawImage(sco, second_1*12, 36, 12, 12, 82, 5, 12, 12);

			background.drawImage(sco, min_0*12, 12, 12, 12, 46, 5, 12, 12);
			background.drawImage(sco, min_1*12, 12, 12, 12, 56, 5, 12, 12);
							
			background.drawImage(sco, hour_0*12, 24, 12, 12, 20, 5, 12, 12);
			background.drawImage(sco, hour_1*12, 24, 12, 12, 30, 5, 12, 12);
		
			background.drawImage(sco, micro_0*12, 0, 12, 12, 95-57+8, 18, 12, 12);
			background.drawImage(sco, micro_1*12, 0, 12, 12, 105-57+8, 18, 12, 12);
		}
	
		function calcul_highscore(){
			// n1/n1 | 0 is faster than Math.Floor
			hour_0 = (h_h/10)%10 | 0;
			hour_1 = (h_h)%10 | 0;
			min_0 = (h_m/10)%10 | 0;
			min_1 = h_m%10 | 0;
			second_0 = (h_s/10)%10 | 0;
			second_1 = h_s%10 | 0;
		}
	
		function calcul_score(){
			// n1/n1 | 0 is faster than Math.Floor
			hour_0 = (hour/10)%10 | 0;
			hour_1 = (hour)%10 | 0;
			min_0 = (min/10)%10 | 0;
			min_1 = min%10 | 0;
			second_0 = (second/10)%10 | 0;
			second_1 = second%10 | 0;
			micro_0 = (micro/10)%10 | 0;
			micro_1 = micro%10 | 0;
		}
	
		function control_bird(){
							
			if (frotter == 0){
				if (bird_y > 45)	bird_y=bird_y-3;

				if (swipe_up == 1){
					if (poss_y_repere > poss_y) follow_y = follow_y + 1;
					else if (poss_y_repere < poss_y) follow_y = follow_y + 1;
									
					if (follow_y > 4){
						follow_y = 0;
						poss_y_repere = poss_y;
						lose_time = 0;
						frotter = 1;
						swipe_down = 0;
						swipe_up = 0;
						swipe_down2 = 0;
						swipe_up2 = 0;
					}
				}			
			}
			else if (frotter == 1){
				if (bird_y < 55) bird_y=bird_y+3;
								
				if (swipe_up == 1){
					if (poss_y_repere > poss_y)	follow_y = follow_y + 1;
					else if (poss_y_repere < poss_y) follow_y = follow_y + 1;
									
					if (follow_y > 4){
						follow_y = 0;
						poss_y_repere = poss_y;
						lose_time = 0;
						frotter = 0;
						swipe_down = 0;
						swipe_up = 0;
						swipe_down2 = 0;
						swipe_up2 = 0;
					}
				}			
			}

		}
	
		function conditions_gameover(){
			show_scale_bird_background();
			background.drawImage(gu, 16, 120); //Draws 'Your score:"
			highshowScore();
			
			background.drawImage(sh, 0, 48);
			background.drawImage(go, 0, 12);
							
			if (touch_time < 20)  touch_time++;
			if (size_y > 0) size_y = size_y - 0.05;
							
			if (size_x > 0){
				bird_x = bird_x + 16;
				bird_y = bird_y + 16;
				size_x = size_x - 0.05;
			}
					
			if (tapped == 1 && touch_time > 15)	{	
				if (poss_y > 45 && poss_y < 70){
					refresh_variables();
				}
				else if (poss_y > 84 && poss_y < 110)
				{
					window.open('https://twitter.com/intent/tweet?text= I have rubbed my bird for '+hour+":"+min+':'+second+'. '+' Can you beat me ? #rubbybird');
					refresh_variables();
				}
			}
		}
	
		function animate_bird(){
			if (time_anim > 5) {
				time_anim = 0;
				graph_bird = graph_bird + 1;
			}
			
			time_anim++;
			if (graph_bird > 3) graph_bird = 0;
		}

		function counter(){
			lose_time = lose_time + 1;
							
			if (lose_time > 50){
				total_sec = 0;	
				high_total_sec = 0;		
				
				total_sec =	total_sec + (hour*3600);		
				total_sec =	total_sec + (min*60);	
				total_sec =	total_sec + second;	
				
				high_total_sec = high_total_sec + (h_h*3600);		 
				high_total_sec = high_total_sec + (h_m*60);		 
				high_total_sec = high_total_sec + (h_s);		 
				
				if (total_sec > high_total_sec){
					h_h = hour;
					h_m = min;
					h_s = second;
					localStorage.setItem("high-hour",hour);
					localStorage.setItem("high-min",min);
					localStorage.setItem("high-second",second);
				}
				
				swipe_down = 0;
				swipe_up = 0;
				tapped = 0;
				touch_time = 0;

				game_mode = 3;	
			}
							
			micro++;
			if (micro > 29){
				micro = 0;
				second++;
			}
							
			if (second > 59){
				second = 0;
				min++;
			}
							
			if (min > 59){
				min = 0;
				hour++;
			}
		}
		
		function refresh_variables(){
			game_mode = 0;
			hour = 0;
			min = 0;
			second = 0;
			lose_time = 0;
			tapped = 0;
			swipe_up = 0;
			swipe_down = 0;
			frotter = 0;
			touch_time = 0;
		}
};  
