var first_layer = document.getElementById('canvas');
var MyC = first_layer.getContext('2d');
var img = new Image();
img.src = 'image.png';
MyC.drawImage(img, 0, 0);