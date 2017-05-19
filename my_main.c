/*========== my_main.c ==========

  This is the only file you need to modify in order
  to get a working mdl project (for now).

  my_main.c will serve as the interpreter for mdl.
  When an mdl script goes through a lexer and parser,
  the resulting operations will be in the array op[].

  Your job is to go through each entry in op and perform
  the required action from the list below:

  push: push a new origin matrix onto the origin stack
  pop: remove the top matrix on the origin stack

  move/scale/rotate: create a transformation matrix
                     based on the provided values, then
		     multiply the current top of the
		     origins stack by it.

  box/sphere/torus: create a solid object based on the
                    provided values. Store that in a
		    temporary matrix, multiply it by the
		    current top of the origins stack, then
		    call draw_polygons.

  line: create a line based on the provided values. Store
        that in a temporary matrix, multiply it by the
	current top of the origins stack, then call draw_lines.

  save: call save_extension with the provided filename

  display: view the image live

  jdyrlandweaver
  =========================*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "parser.h"
#include "symtab.h"
#include "y.tab.h"

#include "matrix.h"
#include "ml6.h"
#include "display.h"
#include "draw.h"
#include "stack.h"

/*======== void first_pass() ==========
  Inputs:
  Returns:

  Checks the op array for any animation commands
  (frames, basename, vary)

  Should set num_frames and basename if the frames
  or basename commands are present

  If vary is found, but frames is not, the entire
  program should exit.

  If frames is found, but basename is not, set name
  to some default value, and print out a message
  with the name being used.

  jdyrlandweaver
  ====================*/
void first_pass() {
  //in order to use name and num_frames
  //they must be extern variables
  extern int num_frames;
  num_frames = 1;
  extern char name[128];

  extern int num_knobs;
  num_knobs = 0;

  int basename = 0;
  int i;

  for (i=0;i<lastop;i++) {

    switch (op[i].opcode)
    {
      //set frame stuff
      case FRAMES:
        num_frames = op[i].op.frames.num_frames;
        printf("num frames set : %d\n",num_frames);
        strcpy(name,"unoriginal");
        break;

      //set the basename
      case BASENAME:
        basename = 1;
        sprintf(name,"anim/%s",op[i].op.basename.p->name);
        break;

      //there is a vary
      case VARY:
        num_knobs++;
    }

    //if no frames but basename or knobs, WRONG
    if ((num_knobs > 0 || basename == 1) && num_frames < 2){
      exit(0);
    }

  }

  return;
}

/*======== struct vary_node ** second_pass() ==========
  Inputs:
  Returns: An array of vary_node linked lists

  In order to set the knobs for animation, we need to keep
  a seaprate value for each knob for each frame. We can do
  this by using an array of linked lists. Each array index
  will correspond to a frame (eg. knobs[0] would be the first
  frame, knobs[2] would be the 3rd frame and so on).

  Each index should contain a linked list of vary_nodes, each
  node contains a knob name, a value, and a pointer to the
  next node.

  Go through the opcode array, and when you find vary, go
  from knobs[0] to knobs[frames-1] and add (or modify) the
  vary_node corresponding to the given knob with the
  appropirate value.

  jdyrlandweaver
====================*/
struct vary_node ** second_pass() {

  //initialize list
  struct vary_node ** knobs = (struct vary_node **)malloc(sizeof(struct vary_node *) * num_frames);

  int i;
  for (i=0;i<lastop;i++) {

    //only care about knobs
    if (op[i].opcode == VARY){

      //add to all relevant frames
      int j;
      if (op[i].op.vary.end_val > op[i].op.vary.start_val){
        printf("%s, increasing from %.f to %.f\n",op[i].op.vary.p->name,op[i].op.vary.start_frame,op[i].op.vary.end_frame);
        for (j = op[i].op.vary.start_frame; j < op[i].op.vary.end_frame; j++){

          //init
          struct vary_node * newKnob = (struct vary_node*)malloc(sizeof(struct vary_node));
          strcpy(newKnob->name,op[i].op.vary.p->name);
          //percentage x endval + startval
          newKnob->value = (j / (op[i].op.vary.end_frame - op[i].op.vary.start_frame)*(op[i].op.vary.end_val) + op[i].op.vary.start_val);

          //add it in
          newKnob->next = knobs[j];
          knobs[j] = newKnob;

        }
      }
      else{
        printf("%s, decreasing from %.f to %.f\n",op[i].op.vary.p->name,op[i].op.vary.start_frame,op[i].op.vary.end_frame);
        for (j = op[i].op.vary.end_frame; j > op[i].op.vary.start_frame; j--){

          //init
          struct vary_node * newKnob = (struct vary_node*)malloc(sizeof(struct vary_node));
          strcpy(newKnob->name,op[i].op.vary.p->name);
          //percentage( = end - current / end - start) x startval + endval
          newKnob->value = ((op[i].op.vary.end_frame - j) / (op[i].op.vary.end_frame - op[i].op.vary.start_frame)*(op[i].op.vary.start_val) + op[i].op.vary.end_val);

          //add it in
          newKnob->next = knobs[j];
          knobs[j] = newKnob;
        }
      }
    }
  }

  return knobs;
}


/*======== void print_knobs() ==========
Inputs:
Returns:

Goes through symtab and display all the knobs and their
currnt values

jdyrlandweaver
====================*/
void print_knobs() {

  int i;

  printf( "ID\tNAME\t\tTYPE\t\tVALUE\n" );
  for ( i=0; i < lastsym; i++ ) {

    if ( symtab[i].type == SYM_VALUE ) {
      printf( "%d\t%s\t\t", i, symtab[i].name );

      printf( "SYM_VALUE\t");
      printf( "%6.2f\n", symtab[i].s.value);
    }
  }
}

/*======== void third_pass() ==========

The main drawing loop, but in its own function now.

====================*/
void third_pass(struct matrix *tmp,struct stack *systems,screen t,color g){

  int i,j;
  double step = 0.1;
  double theta;

  for (i=0;i<lastop;i++) {

    printf("%d: ",i);
    //reset knobVal
    double knobVal = 1;
      switch (op[i].opcode)
	{
	case SPHERE:
	  printf("Sphere: %6.2f %6.2f %6.2f r=%6.2f",
		 op[i].op.sphere.d[0],op[i].op.sphere.d[1],
		 op[i].op.sphere.d[2],
		 op[i].op.sphere.r);

	  if (op[i].op.sphere.constants != NULL)
	    {
	      printf("\tconstants: %s",op[i].op.sphere.constants->name);
	    }
	  if (op[i].op.sphere.cs != NULL)
	    {
	      printf("\tcs: %s",op[i].op.sphere.cs->name);
	    }
	  add_sphere(tmp, op[i].op.sphere.d[0],
		     op[i].op.sphere.d[1],
		     op[i].op.sphere.d[2],
		     op[i].op.sphere.r, step);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, g);
	  tmp->lastcol = 0;
	  break;
	case TORUS:
	  printf("Torus: %6.2f %6.2f %6.2f r0=%6.2f r1=%6.2f",
		 op[i].op.torus.d[0],op[i].op.torus.d[1],
		 op[i].op.torus.d[2],
		 op[i].op.torus.r0,op[i].op.torus.r1);

	  if (op[i].op.torus.constants != NULL)
	    {
	      printf("\tconstants: %s",op[i].op.torus.constants->name);
	    }
	  if (op[i].op.torus.cs != NULL)
	    {
	      printf("\tcs: %s",op[i].op.torus.cs->name);
	    }
	  add_torus(tmp,
		    op[i].op.torus.d[0],
		    op[i].op.torus.d[1],
		    op[i].op.torus.d[2],
		    op[i].op.torus.r0,op[i].op.torus.r1, step);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, g);
	  tmp->lastcol = 0;
	  break;
	case BOX:
	  printf("Box: d0: %6.2f %6.2f %6.2f d1: %6.2f %6.2f %6.2f",
		 op[i].op.box.d0[0],op[i].op.box.d0[1],
		 op[i].op.box.d0[2],
		 op[i].op.box.d1[0],op[i].op.box.d1[1],
		 op[i].op.box.d1[2]);
	  if (op[i].op.box.constants != NULL)
	    {
	      printf("\tconstants: %s",op[i].op.box.constants->name);
	    }
	  if (op[i].op.box.cs != NULL)
	    {
	      printf("\tcs: %s",op[i].op.box.cs->name);
	    }
	  add_box(tmp,
		  op[i].op.box.d0[0],op[i].op.box.d0[1],
		  op[i].op.box.d0[2],
		  op[i].op.box.d1[0],op[i].op.box.d1[1],
		  op[i].op.box.d1[2]);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, g);
	  tmp->lastcol = 0;
	  break;
	case LINE:
	  printf("Line: from: %6.2f %6.2f %6.2f to: %6.2f %6.2f %6.2f",
		 op[i].op.line.p0[0],op[i].op.line.p0[1],
		 op[i].op.line.p0[1],
		 op[i].op.line.p1[0],op[i].op.line.p1[1],
		 op[i].op.line.p1[1]);
	  if (op[i].op.line.constants != NULL)
	    {
	      printf("\n\tConstants: %s",op[i].op.line.constants->name);
	    }
	  if (op[i].op.line.cs0 != NULL)
	    {
	      printf("\n\tCS0: %s",op[i].op.line.cs0->name);
	    }
	  if (op[i].op.line.cs1 != NULL)
	    {
	      printf("\n\tCS1: %s",op[i].op.line.cs1->name);
	    }
	  break;
	case MOVE:
	  printf("Move: %6.2f %6.2f %6.2f",
		 op[i].op.move.d[0],op[i].op.move.d[1],
		 op[i].op.move.d[2]);
	  if (op[i].op.move.p != NULL)
    {
      printf("\tknob: %s",op[i].op.move.p->name);
      for ( j=0; j < lastsym; j++ ) {

        if ( strcmp(symtab[j].name,op[i].op.move.p->name) == 0 ) {
          knobVal = symtab[j].s.value;
          break;
        }
      }
    }
	  tmp = make_translate( op[i].op.move.d[0]*knobVal,
				op[i].op.move.d[1] * knobVal,
				op[i].op.move.d[2] * knobVal);
	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
    break;
	case SCALE:
	  printf("Scale: %6.2f %6.2f %6.2f",
		 op[i].op.scale.d[0],op[i].op.scale.d[1],
		 op[i].op.scale.d[2]);
	  if (op[i].op.scale.p != NULL)
	    {
	      printf("\tknob: %s",op[i].op.scale.p->name);
        for ( j=0; j < lastsym; j++ ) {

          if ( strcmp(symtab[j].name,op[i].op.scale.p->name) == 0 ) {
            knobVal = symtab[j].s.value;
            break;
          }
        }
	    }
	  tmp = make_scale( op[i].op.scale.d[0]*knobVal,
			    op[i].op.scale.d[1]*knobVal,
			    op[i].op.scale.d[2]*knobVal);
	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
    break;
	case ROTATE:
	  printf("Rotate: axis: %6.2f degrees: %6.2f",
		 op[i].op.rotate.axis,
		 op[i].op.rotate.degrees);
	  if (op[i].op.rotate.p != NULL)
    {
      printf("\tknob: %s",op[i].op.rotate.p->name);
      for ( j=0; j < lastsym; j++ ) {

        if ( strcmp(symtab[j].name,op[i].op.rotate.p->name) == 0 ) {
          knobVal = symtab[j].s.value;
          break;
        }
      }
    }
	  theta =  op[i].op.rotate.degrees * (M_PI / 180);
	  if (op[i].op.rotate.axis == 0 )
	    tmp = make_rotX( theta * knobVal);
	  else if (op[i].op.rotate.axis == 1 )
	    tmp = make_rotY( theta * knobVal );
	  else
	    tmp = make_rotZ( theta * knobVal );

	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
	  break;
	case PUSH:
	  printf("Push");
	  push(systems);
	  break;
	case POP:
	  printf("Pop");
	  pop(systems);
	  break;
	case SAVE:
	  printf("Save: %s",op[i].op.save.p->name);
	  save_extension(t, op[i].op.save.p->name);
	  break;
	case DISPLAY:
	  printf("Display");
	  display(t);
	  break;
    }
      printf("\n");
    }
}


/*======== void my_main() ==========
  Inputs:
  Returns:

  This is the main engine of the interpreter, it should
  handle most of the commadns in mdl.

  If frames is not present in the source (and therefore
  num_frames is 1, then process_knobs should be called.

  If frames is present, the enitre op array must be
  applied frames time. At the end of each frame iteration
  save the current screen to a file named the
  provided basename plus a numeric string such that the
  files will be listed in order, then clear the screen and
  reset any other data structures that need it.

  Important note: you cannot just name your files in
  regular sequence, like pic0, pic1, pic2, pic3... if that
  is done, then pic1, pic10, pic11... will come before pic2
  and so on. In order to keep things clear, add leading 0s
  to the numeric portion of the name. If you use sprintf,
  you can use "%0xd" for this purpose. It will add at most
  x 0s in front of a number, if needed, so if used correctly,
  and x = 4, you would get numbers like 0001, 0002, 0011,
  0487

  jdyrlandweaver
  ====================*/
void my_main() {

  //declare
  struct matrix *tmp;
  struct stack *systems;
  screen t;
  color g;

  //initialize
  systems = new_stack();
  tmp = new_matrix(4, 1000);
  clear_screen( t );
  g.red = 0;
  g.green = 0;
  g.blue = 0;

  //prelim
  first_pass();
  printf("first pass complete\n");
  struct vary_node ** knobs = second_pass();
  printf("second pass complete\n");
  //animation
  if (num_frames > 1){

    int i;
    //for each frame
    for (i = 0; i < num_frames; i++){

      //deal w/knobs
      struct vary_node * head = knobs[i];
      int j;
      while (head){
        for ( j=0; j < lastsym; j++ ) {

          if ( strcmp(symtab[j].name,head->name) == 0 ) {
            symtab[j].s.value = head->value;
            break;
          }
        }
        head = head->next;
      }

      //reinitialize
      systems = new_stack();
      tmp = new_matrix(4, 1000);
      clear_screen( t );
      g.red = 0;
      g.green = 0;
      g.blue = 0;

      //--the name of this file--
      //number
      char s[128];
      sprintf (s, "%03d", i );
      //combine w/name
      char n[128];
      sprintf(n,"%s%s%s\n",name,s,".png");

      third_pass(tmp, systems, t, g);
      //print_knobs();

      //print
      printf("Saving %s\n",n);
      //save
      save_extension(t, n);

      free_stack(systems);
      free_matrix(tmp);

    }
  }
  //static
  else{
    third_pass(tmp, systems, t, g);
  }

}
