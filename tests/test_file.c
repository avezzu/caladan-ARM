#include <stdio.h>




int main(){
  FILE *f = fopen("//home//ubuntu//caladan-aarch64//tests//text.txt", "w");
  fprintf(f, "Hello World!");
  fclose(f);
  return 0;
}
