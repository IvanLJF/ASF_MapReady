/* Unit tests for the asf_meta library.  These use the 'check' unit
   testing framework for C.  Not complete by any means, but hopefully
   new code will use them at least.  */

/* Note that these tests have wired in the values from the test meta
   file.  */

#include <asf_meta.h>
#include <check.h>		/* For unit check functions.  */

/* Over-safe micron for test floating point comparisons.  */
#define UNIT_TESTS_MICRON 0.00001
#define UNIT_TESTS_FLOAT_COMPARE(a, b) (abs(a - b) < UNIT_TESTS_MICRON ? 1 : 0)

/* Test part of meta_read that parses new files.  */
START_TEST(test_meta_read_new_format)
{
  meta_parameters *meta;
  
  meta_read(meta, "test_file.meta");
  
  /* Check a random fields to make sure things are working.  */
  fail_unless(meta->general->orbit == 123, 
	      "orbit field from general block not read correctly");
  
  /* Check a not-so-random field: things from projection params block
     are currently partly holdover from deprecated code and use a
     union.  */
  fail_unless(meta->projection->type == 'A' 
	      && meta->projection.param.atct.alpha1 == 0.6,
	      "alpha1 field from param->atct block not read correctly");

  /* Another not-so-random field check: state vector blocks currently
     use wierd field names in the data file and map strangely into a
     dynamicly allocated internal structure, lots of possibility for
     error.  */
  fail_unless( UNIT_TESTS_FLOAT_COMPARE(meta->state_vectors.vecs[1].pos.y, 
					22333),
	       "Y position element of second state vector not read correctly");

  meta_free(meta);
}


/* Machinery for running the 'check' tests.  */
Suite *asf_meta_suite(void)
{
  Suite *s = suite_create("asf_meta");
  TCase *tc_core = tcase_create("Core");
  
  suite_add_tcase(s, tc_core);
  
  tcase_add_test(tc_core, test_meta_read_new_format);

  return s;
}

int main(void)
{
  int nf;
  Suite *s = asf_meta_suite();
  SRunner *sr = srunner_create(s);
 
  srunner_run_all(sr, CK_NORMAL);

  nf = srunner_ntests_failed(sr);

  srunner_free(sr);
  suite_free(s);

  return ( nf == 0 ) ? EXIT_SUCCESS : EXIT_FAILURE;
}






